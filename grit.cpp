#include<cerrno>
#include<exception>
#include<stdexcept>
#include<termios.h>
#include<unistd.h>
#include<iostream>
#include<stdio.h>
#include<sys/ioctl.h>
#include<sstream>
#include<cstring>
#include<string>
#include<fstream>
constexpr char ESC = '\x1b';
constexpr char ctrl_q = '\x11';
//owns the terminal's raw mode: constructing it turns raw mode on, destroying it turns it off
class Raw_Mode{

    private:
        //oldt = the original terminal settings (kept so we can restore them), newt = the raw-mode copy we build
        struct termios oldt, newt;

        //read the current terminal settings for standard input into oldt
        int grab(){
            bool OT_GRAB = tcgetattr(STDIN_FILENO, &oldt);
            if (OT_GRAB){
                throw std::runtime_error("Error getting terminal attributes\n");
            }
            return 0;
        }

        //apply the raw-mode settings in newt to the terminal
        int set(){
            if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == -1){
                throw std::runtime_error("Error setting terminal attributes\n");
            }
            return 0;
        }

        //put the original settings (oldt) back; the destructor calls this
        int return_terminal(){
            if (tcsetattr(STDIN_FILENO, TCSANOW, &oldt) == -1){
                perror("Failure destructing");
                return errno;
            }
            return 0;
        }

    public:
        //runs when the object is created: save the settings, then switch the terminal to raw mode
        Raw_Mode(){
            grab();
            //copy the original settings, then turn the copy into raw mode
            newt = oldt;
            cfmakeraw(&newt);
            newt.c_cc[VMIN] = 0;
            newt.c_cc[VTIME] = 1;
            set();
        }

        //runs automatically when the object goes out of scope: restore the terminal
        ~Raw_Mode(){
            return_terminal();
        }

        //no copies: each copy's destructor would restore the terminal on its own
        Raw_Mode(const Raw_Mode&) = delete;
        Raw_Mode& operator=(const Raw_Mode&) = delete;
};

//owns a file's lines in memory we allocate and free by hand (Part A: the "jagged array")
//
//  lines ──► [ p0 | p1 | p2 | empty | empty ]      line_count = 3, capacity = 5
//               │    │    │
//               ▼    ▼    ▼
//         "#include<cerrno>\0"   "\0"   "int main(){\0"     ← each line is its own new char[]
//
//  lines   is a char**  = "the address of an array of addresses"
//  lines[i] is a char*  = "the address of line i's first character"   (indexing removes one *)
class Lines{
    private:
        //nullptr = "points at nothing yet"; delete[] on nullptr is safe and does nothing
        char** lines = nullptr;
        //how many slots are used; also the index of the next free slot
        int line_count = 0;
        //how many slots the array has in total; when line_count reaches it, grow()
        int capacity = 0;

        //an allocation can't get bigger, so: make a new bigger array, copy the addresses over, free the old one
        void grow(){
            //double the room, or start at 8 the first time (2 * 0 is still 0)
            capacity = 2 * capacity;
            if (capacity == 0) capacity = 8;

            //new char*[n] = "room for n ADDRESSES of lines" → gives back a char**
            //(compare add(): new char[n] = "room for n CHARACTERS" → gives back a char*)
            char** bigger = new char*[capacity];

            //copy each address (the arrow), not the text: slot i of bigger now points at the same line as slot i of lines
            for (int i = 0; i < line_count; i++){
                bigger [i] = lines[i];
            }
            //free the OLD array of addresses only; the lines it pointed at are untouched (bigger still points at them)
            //this must come before the next line: once lines is overwritten, nothing points at the old array to free it
            delete[] lines;
            //copy the new array's ADDRESS into the member; when grow() returns, the local name "bigger" disappears
            //but the array itself stays, because new[] memory only goes away with delete[]
            lines = bigger;
        }
    public:
        //declaring any constructor (even a deleted copy one) removes the free default one; this asks for it back
        Lines() = default;

        //runs automatically when the Lines object goes out of scope: free everything, lines first, then the array
        //(the array is the only way to reach the lines, so freeing it first would leak every line)
        ~Lines(){
            for (int i = 0; i < line_count; i++){
                //each line came from new char[...] in add(), so it needs delete[] (the [] means "this was an array")
                delete [] lines[i];
            }
            //the array of addresses came from new char*[...] in grow(), so it also needs delete[]
            delete[] lines;

        }

        //no copies: two objects holding the same addresses would both delete[] them (a double free)
        Lines(const Lines&) = delete;
        Lines& operator = (const Lines&) = delete;

        //copy one line of text into memory we own, and store its address in the next free slot
        void add(std::string text){
            //full? make room first, so lines[line_count] below is never past the end
            if (line_count == capacity){
                grow();
            }
            //room for the characters plus 1 for the '\0' that marks the end of a C string → a char*
            char* line = new char[text.size() + 1];
            //"the next free slot of lines now holds the address of this new line"   (char* = char*)
            lines[line_count] = line;
            //copy text's characters, including the '\0', into our memory (destination first, then source)
            //we must copy: text is a local std::string and is destroyed when add() returns
            std::strcpy(line, text.c_str());
            //that slot is now used; line_count also becomes the next free slot
            line_count++;
        }

        //give back the address of line i's first character (the caller can print it like any C string)
        char* get(int i){
            return lines[i];
        }

        //how many lines are stored
        int size(){
            return line_count;
        }
};

//the editor's state (cursor, screen size, scroll position, the file's lines) and everything that uses it
struct Editor{
    //cursor position on the screen; both count from 1, like the terminal
    int x = 1;
    int y = 1;
    //screen size, filled in by window_size() on every redraw
    int max_row;
    int max_col;
    //the file line shown on the top row (0 = the file's first line)
    //the file line under the cursor is offset + y - 1
    int offset = 0;
    //the file's lines; a member, so they live as long as the editor does
    Lines hold_lines;

    //draw one full screen: clear, one file line (or ~ past the end) per row,
    //the welcome message if no lines were loaded, then the cursor at (y, x)
    void window_size(){
        struct winsize w;
        //ask the kernel for the window size (rows and columns) into w
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1){
            std::cout << "\033[2J\033[H" << std::flush;
            throw std::runtime_error("Error getting terminal window size\n");
        }

        max_row = w.ws_row;
        max_col = w.ws_col;

        //the message sits one third of the way down
        int msg_row = (max_row / 3);

        std::ostringstream message;
        message << "WELCOME TO BLAISE TERMINAL";
        std::string welcome = message.str();
        //center = middle of the screen minus half the message; done in int so it can go negative safely
        int Swelcome = static_cast<int>(welcome.size());
        int msg_col = (max_col / 2) - (Swelcome / 2);
        //cut the message to the screen width so it never wraps
        std::string wsub = welcome.substr(0,max_col);

        //columns start at 1, so never send a column below that
        if (msg_col < 1){
            msg_col = 1;
        }

        //clear the screen and go to the top left
        std::cout << "\033[2J\033[H" << std::flush;
        //screen row i shows file line offset + i, or ~ past the end of the file
        //no newline after the last row, or the screen would scroll up
        for (int i = 0; i < w.ws_row; ++i){
            if (offset + i < hold_lines.size()){
                std::string show_lines (hold_lines.get(offset +i));
                //cut the line to the screen width so it never wraps onto the next row
                std::string lsub = show_lines.substr(0, max_col);
                std::cout << lsub;
            }
            else{
                std::cout << "~";
            }
            std::cout << "\r";
            if (i < w.ws_row -1){
                std::cout << "\n";
            }
        }

        //no lines loaded: show the welcome message on top of the ~ rows
        if (hold_lines.size() < 1){
            std::cout << "\033[" << msg_row << ";" << msg_col << "H" << wsub << "\r";
        }

        //put the terminal's cursor where the editor's cursor is
        std::cout << "\033[" << y << ";" << x << "H" << std::flush;
    }

    //the keys the editor understands; anything else is "other"
    enum class Keys{
        arrow_up,
        arrow_down,
        arrow_right,
        arrow_left,
        quit,
        other,
        pg_up,
        pg_down,
        home,
        end,
        ctrl_end,
    };

    //wait for one key and say which one it was
    Keys read_keys(){
        char seq[5];
        char ch;
        //VMIN=0/VTIME=1: read gives back 0 after 0.1s with no key, so keep asking until one arrives
        ssize_t n = 0;
        while (n == 0){
            n = read(STDIN_FILENO, &ch, 1);
        }
        if(n == -1){
            throw std::runtime_error("Error reading input\n");
        }
        if (ch == ctrl_q) return Keys::quit;
        if (ch == ESC){
            //the rest of an escape sequence arrives right after the ESC; s = how many bytes came
            //(0 = a lone Esc press), so only seq[0] .. seq[s-1] belong to this key
            ssize_t s = read(STDIN_FILENO, seq, 5);
            if (s == 0){
                return Keys::other;
            }
            if(s == -1){
                throw std::runtime_error("Error reading input\n");
            }

            if (s > 1 && seq[0] == '['){
                if (seq[1] == 'A') return Keys::arrow_up;
                if (seq[1] == 'B') return Keys::arrow_down;
                if (seq[1] == 'C') return Keys::arrow_right;
                if (seq[1] == 'D') return Keys::arrow_left;
                if (seq[1] == 'H') return Keys::home;
                if (seq[1] == 'F') return Keys::end;
                //Ctrl-End = ESC [ 1 ; 5 F
                if (s > 2 && seq[1] == '1'){
                    if (s > 3 && seq[2] == ';'){
                        if (s > 4 && seq[3] == '5'){
                            if (seq[4] == 'F') return Keys::ctrl_end;
                        }
                    }
                }
                //Page Up = ESC [ 5 ~, Page Down = ESC [ 6 ~
                if (s > 2 && seq[1] == '5'){
                    if ( seq[2] == '~') return Keys::pg_up;
                }
                if (s > 2 && seq[1] == '6'){
                    if (seq[2] == '~') return Keys::pg_down;
                }
            }
            else{
                return Keys::other;
            }
        }

        return Keys::other;
    }

    //the editor's main loop: wait for a key, update the cursor and scroll, redraw; until ctrl-q
    int keyboard(){
        window_size();
        while (true){
            //each key only changes the state (x, y, offset); the checks below keep it valid
            switch(read_keys()){
                case Keys::arrow_up : y--; break;
                case Keys::arrow_down : y++; break;
                case Keys::arrow_right : x++; break;
                case Keys::arrow_left : x--; break;
                case Keys::quit : std::cout << "\033[2J\033[H" << std::flush; return 1;
                case Keys::other : break;
                case Keys::pg_up : y = 1; break;
                case Keys::pg_down : y = max_row; break;
                case Keys::home : x = 1; break;
                case Keys::end : x = max_col; break;
                case Keys::ctrl_end :
                    //show the last screenful of the file (or from the top, if it all fits), cursor on the bottom row
                    offset = hold_lines.size() - max_row;
                    if (offset < 0){
                        offset = 0;
                    }
                    y = max_row;
                    break;
            }

            //above the top row: stay on row 1 and scroll up a line, if there is one above
            if (y < 1){
                if (offset > 0){
                    offset = offset - 1;
                }
                y = 1;
            }

            //below the bottom row: stay on the bottom row and scroll down a line, if there is one below
            if (y > max_row){
                if (offset + max_row < hold_lines.size()){
                    offset = offset + 1;
                }
                y = max_row;
            }

            //past the file's last line: put the cursor back on it (no lines = nothing to stop at)
            if (offset + y > hold_lines.size() && hold_lines.size() > 0){
                y = hold_lines.size() - offset;
            }

            if (x > max_col){
                x = max_col;
            }

            if (x < 1){
                x = 1;
            }

            window_size();
        }
        return 0;
    }
};

int main(int argc, char *argv[]){
    try{
        //created first, so its lines outlive the loading block below and stay for the whole run
        Editor e_obj;
        //argc = how many words were typed (including the program's name); argv = those words
        //argv is an array of char* (one address per word): argv[0] = "./grit", argv[1] = the filename
        //load before raw mode starts, so an error message prints normally
        if (argc > 1){
            //open the file for reading; file is a local, so it closes itself when this block ends (RAII)
            std::ifstream file(argv[1]);
            //a stream counts as false if opening failed (missing file, no permission, ...)
            if (!file){
                std::cerr << argv[0] << ": " << argv[1] << ": " << strerror(errno) << "\n";
                return 1;
            }
            //row is a std::string (an object, not a pointer), reused for every line
            std::string row;
            //getline reads one line into row and removes the '\n'; it returns the stream,
            //which counts as true while a line was read and false when there are none left
            while (std::getline(file, row)){
                //strip a Windows '\r' left at the end; the line must NOT be empty first, because back() on an empty string is undefined
                if (!row.empty() && row.back() == '\r'){
                    row.pop_back();
                }
                //copy the line into the editor's own memory
                e_obj.hold_lines.add(row);
            }
        }

        //raw mode is on for as long as obj exists, so keyboard() must run inside this block
        Raw_Mode obj;

        e_obj.keyboard();
        //leaving try destroys obj (terminal restored), then e_obj → ~Lines() frees every line and the array
    }
    //any throw above lands here; obj's destructor has already restored the terminal
    catch(const std::exception& e){
        std::cout << e.what() << strerror(errno) << "\n";
        return 1;
    }
    return 0;
}
