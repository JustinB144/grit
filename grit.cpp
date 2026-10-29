#include <cctype>
#include <climits>
#include <vector>
#include <cerrno>
#include <exception>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sstream>
#include <cstring>
#include <string>
#include <fstream>

//the raw bytes the keyboard sends for the keys we handle by hand
constexpr char ESC = '\x1b';
constexpr char ctrl_q = '\x11';
constexpr char TAB = '\x09';
constexpr char BACKSPACE = '\x7f';
constexpr char ENTER = '\x0d';
constexpr char CTRL_S = '\x13';
constexpr char CTRL_X = '\x18';

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

//screen columns from one tab stop to the next
constexpr int TAB_STOP = 4;

//how many screen columns the first `index` characters of `line` take up; that's also the
//screen column (from 0) where character `index` is drawn. a tab jumps to the next tab stop
int render_col(const std::string& line, int index){
    int col = 0;
    for (int i = 0; i < index && i < static_cast<int>(line.size()); ++i){
        if (line[i] == TAB){
            col = col + (TAB_STOP - (col % TAB_STOP));
        }
        else{
            ++col;
        }
    }
    return col;
}

//the line as it appears on screen: every tab becomes spaces up to the next tab stop
//(the same rule as render_col, so the cursor and the text always agree)
std::string render(const std::string& line){
    std::string out;
    for (char ch : line){
        if (ch == TAB){
            out += ' ';
            while (out.size() % TAB_STOP != 0){
                out += ' ';
            }
        }
        else{
            out += ch;
        }
    }
    return out;
}

//the editor's state (cursor, screen size, scroll position, the file's lines) and everything that uses it
struct Editor{
    //cursor position on the screen; both count from 1, like the terminal
    int x = 1;
    int y = 1;
    //screen size, filled in by window_size() on every redraw;
    //max_text_row = the rows used for text (every row but the last, which holds the status bar)
    int max_row = 0;
    int max_text_row = 0;
    int max_col = 0;
    //the file's lines, one string per line, without line endings
    std::vector<std::string> hold_lines;
    //the file line shown on the top row (0 = the file's first line);
    //the file line under the cursor is offset + y - 1
    int offset = 0;
    //how many screen columns the cursor's line is shifted left (only that line is ever shifted)
    int coffset = 0;
    //the cursor's position in the text: which character of the line it's on (a tab counts as 1)
    int c_index = 0;
    //how many lines the file has
    int kv = 0;
    //the last byte read_keys() read; Keys::other uses it to know which character was typed
    char ch = 0;
    //the file line (from 0) under the cursor before this key
    int line = 0;
    //the name shown in the status bar ("no name" when no file was given)
    std::string file_name;
    //true when the lines are different from how they were when the editor started
    bool edit = false;
    std::string msg;
    //draw the status bar on the bottom row in reverse video:
    //file name, number of lines, cursor line and column (both from 0), and [MODIFIED] if edited
    void last_row(){
        std::ostringstream info;
        if (msg.size() > 0){
        	info << "\033[" << max_row << ";1H" << "\033[7m"
         		 << msg 
         		 <<  "\033[0m";
        }
        else if (edit){
            info << "\033[" << max_row << ";1H" << "\033[7m"
                 << "File: [" << file_name << "] line #: [" << kv << "] Line: [" << line << "] col: [" << c_index << "]"
                 << " [MODIFIED]" << "\033[0m";
        }
        else info << "\033[" << max_row << ";1H" << "\033[7m"
                  << "File: [" << file_name << "] line #: [" << kv << "] Line: [" << line << "] col: [" << c_index << "]"
                  << "\033[0m";
        std::string result = info.str();
        std::cout << result;
    }

    //no lines to type into (no file, or an empty one): make one empty line and put the cursor on it
    void no_file(){
        if (kv == 0){
            hold_lines.push_back("");
            kv = 1;
            y = 1;
            offset = 0;
            c_index = 0;
            line = 0;
        }
    }

    //draw one full screen: clear, one file line (or ~ past the end) per text row, the status bar,
    //the welcome message if there are no lines, then the cursor at (y, x)
    void window_size(){
        struct winsize w;

        int wv = static_cast<int>(hold_lines.size());
        //ask the kernel for the window size (rows and columns) into w
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1){
            std::cout << "\033[2J\033[H" << std::flush;
            throw std::runtime_error("Error getting terminal window size\n");
        }
        max_row = w.ws_row;
        max_col = w.ws_col;
        max_text_row = w.ws_row - 1;

        //the message sits one third of the way down the text rows
        int msg_row = (max_text_row / 3);

        std::ostringstream message;
        message << "WELCOME TO BLAISE TERMINAL";
        std::string welcome = message.str();
        //center = middle of the screen minus half the message; done in int so it can go negative safely
        int Swelcome = static_cast<int>(welcome.size());
        int msg_col = (max_col / 2) - (Swelcome / 2);
        //cut the message to the screen width so it never wraps
        std::string wsub = welcome.substr(0, max_col);

        //columns start at 1, so never send a column below that
        if (msg_col < 1){
            msg_col = 1;
        }

        //clear the screen and go to the top left
        std::cout << "\033[2J\033[H" << std::flush;
        //text row i shows file line offset + i, or ~ past the end of the file;
        //each one ends with a newline, which lands on the status bar row, never past the screen
        for (int i = 0; i < max_text_row; ++i){
            if (offset + i < wv && wv > 0){
                //the line as it looks on screen (tabs expanded), so cutting by size cuts by screen columns
                std::string shown = render(hold_lines.at(offset + i));
                if (i == y - 1){
                    //the cursor's line: start coffset columns in, so the part with the cursor is visible
                    if (coffset < static_cast<int>(shown.size())){
                        std::cout << shown.substr(coffset, max_col);
                    }
                }
                //every other line starts at column 0; if it's too long, cut it and mark the last column with >
                else if (static_cast<int>(shown.size()) > max_col){
                    std::cout << shown.substr(0, max_col - 1) << ">";
                }
                else{
                    std::cout << shown;
                }
            }
            else{
                std::cout << "~";
            }
            std::cout << "\r";
            std::cout << "\n";   
        }
        last_row();
        //no lines loaded: show the welcome message on top of the ~ rows
        if (wv < 1){
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
        del,
        backspace,
        esc,
        enter,
        command_line,
    };

    //wait for one key and say which one it was
    Keys read_keys(){
        char seq[5];

        //VMIN=0/VTIME=1: read gives back 0 after 0.1s with no key, so keep asking until one arrives
        ssize_t n = 0;
        while (n == 0){
            n = read(STDIN_FILENO, &ch, 1);
        }
        if (n == -1){
            throw std::runtime_error("Error reading input\n");
        }

        if (ch == ctrl_q) return Keys::quit;
        if (ch == BACKSPACE) return Keys::backspace;
        if (ch == ENTER) return Keys::enter;
        if (ch == CTRL_X) return Keys::command_line;
        if (ch == ESC){
            //the rest of an escape sequence arrives right after the ESC; take only the first 2 bytes
            //(a letter ends the key; a digit means more bytes follow), s = how many came (0 = a lone Esc press)
            //reading only what this key needs leaves the next key's bytes alone when keys pile up
            ssize_t s = read(STDIN_FILENO, seq, 2);
            if (s == 0){
                return Keys::esc;
            }
            if (s == -1){
                throw std::runtime_error("Error reading input\n");
            }

            if (s > 1 && seq[0] == '['){
                if (seq[1] == 'A') return Keys::arrow_up;
                if (seq[1] == 'B') return Keys::arrow_down;
                if (seq[1] == 'C') return Keys::arrow_right;
                if (seq[1] == 'D') return Keys::arrow_left;
                if (seq[1] == 'H') return Keys::home;
                if (seq[1] == 'F') return Keys::end;

                //Ctrl-End = ESC [ 1 ; 5 F: read the last 3 bytes into seq[2] .. seq[4]
                if (seq[1] == '1'){
                    ssize_t s3 = read(STDIN_FILENO, &seq[2], 3);
                    if (s3 == -1){
                        throw std::runtime_error("Error reading input\n");
                    }
                    //fewer than 3 = a broken sequence
                    if (s3 < 3) return Keys::other;
                    if (seq[2] == ';' && seq[3] == '5' && seq[4] == 'F') return Keys::ctrl_end;
                }
                //Page Up = ESC [ 5 ~, Page Down = ESC [ 6 ~, Delete = ESC [ 3 ~: read the last byte into seq[2]
                if (seq[1] == '5' || seq[1] == '6' || seq[1] == '3'){
                    ssize_t s1 = read(STDIN_FILENO, &seq[2], 1);
                    if (s1 == -1){
                        throw std::runtime_error("Error reading input\n");
                    }
                    if (s1 < 1) return Keys::other;
                    //seq[1] says which key it is
                    if (seq[1] == '5' && seq[2] == '~') return Keys::pg_up;
                    if (seq[1] == '6' && seq[2] == '~') return Keys::pg_down;
                    if (seq[1] == '3' && seq[2] == '~') return Keys::del;
                }
            }
        }

        return Keys::other;
    }
    
    int command_line(std::ostringstream& cm_message){
    
   	
    while (true){
    	
     	msg << ":" << msg;
      	window_size();
       	std::cout << "\033[" << max_row << ";1H";
        switch (read_keys()){
        	case Keys::esc : 
         		
         		break;

        	case Keys::enter :
        	
            	hold_lines[line].erase(hold_lines[line].begin() + 1, hold_lines[line].end());
             	
              	break;

        	case Keys::other :
	            //a printable character or a tab: insert it at the cursor
	            if (std::isprint(static_cast<unsigned char>(ch)) || ch == TAB){
	                hold_lines[line].insert(c_index, 1, ch);
	                ++c_index;
	            }
	            break;
													
        	case Keys::backspace :
			    if (kv > 0){
			        //delete the character before the cursor
			        if (c_index > 0){
			            hold_lines[line].erase(c_index - 1, 1);
			            --c_index;
			        }
			        //at the start of a line: join it onto the end of the line above
			        else if (c_index < 0){
			            c_index = 0;
			        }
			    }
			    break;
        }
     	}
       return 0;
    }
    //the editor's main loop: wait for a key, update the text, cursor and scroll, redraw; until ctrl-q
    int keyboard(){
        window_size();
        kv = static_cast<int>(hold_lines.size());
        //a copy of the lines as they started, to tell whether they've been modified
        std::vector<std::string> og_vec;
        og_vec = hold_lines;
        while (true){
        line = offset + y - 1;
        int text_length = 0;

        if (kv > 0){
            text_length = static_cast<int>(hold_lines[line].size());
        }
        //each key only changes the state (the lines, kv, y, c_index, offset); the checks below keep it valid
        switch (read_keys()){
            case Keys::arrow_up : y--; break;
            case Keys::arrow_down : y++; break;
            case Keys::arrow_right :
                if (c_index < text_length){
                    ++c_index;
                }
                //at the end of a line: go to the start of the next one
                else if (line < kv - 1 && kv > 0){
                    y++;
                    c_index = 0;
                }
                break;
            case Keys::arrow_left :
                if (c_index > 0) c_index--;
                //at the start of a line: go to the end of the one above (the clamp below finds the end)
                else if (line > 0 && kv > 0){
                    y--;
                    c_index = INT_MAX;
                }
                break;
            case Keys::quit : std::cout << "\033[2J\033[H" << std::flush; return 1;
            case Keys::other :
                //a printable character or a tab: insert it at the cursor
                if (std::isprint(static_cast<unsigned char>(ch)) || ch == TAB){
                    no_file();
                    hold_lines[line].insert(c_index, 1, ch);
                    ++c_index;
                }
                break;
            case Keys::backspace :
                if (kv > 0){
                    //delete the character before the cursor
                    if (c_index > 0){
                        hold_lines[line].erase(c_index - 1, 1);
                        --c_index;
                    }
                    //at the start of a line: join it onto the end of the line above
                    else if (line > 0){
                        std::string& prior_line = hold_lines[line - 1];
                        int prior_length = static_cast<int>(prior_line.size());
                        prior_line += hold_lines[line];
                        hold_lines.erase(hold_lines.begin() + line);
                        kv = kv - 1;
                        y--;
                        c_index = prior_length;
                    }
                }
                break;
            case Keys::pg_up : y = 1; break;
            case Keys::pg_down : y = max_text_row; break;
            case Keys::home : c_index = 0; break;
            case Keys::end : c_index = INT_MAX; break;
            case Keys::del :
                if (kv > 0){
                    //delete the character under the cursor
                    if (c_index < text_length){
                        hold_lines[line].erase(c_index, 1);
                    }
                    //at the end of a line: join the line below onto this one
                    else if (offset + y < kv){
                        std::string& post_line = hold_lines[line + 1];
                        hold_lines[line] += post_line;
                        hold_lines.erase(hold_lines.begin() + (line + 1));
                        kv = kv - 1;
                    }
                }
                break;
            case Keys::ctrl_end :
                //show the last screenful of the file (or from the top, if it all fits), cursor on the bottom row
                offset = kv - max_text_row;
                if (offset < 0){
                    offset = 0;
                }
                y = max_text_row;
                break;
            case Keys::enter :
                //split the line at the cursor: the part after it moves down to a new line
                no_file();
                if (kv > 0){
                    std::string copy = hold_lines[line].substr(c_index);
                    hold_lines[line].erase(c_index);
                    hold_lines.insert(hold_lines.begin() + line + 1, copy);
                    kv = kv + 1;
                    y++;
                    c_index = 0;
                }
                break;
            case Keys::esc : break;
        }

        //above the top row: stay on row 1 and scroll up a line, if there is one above
        if (y < 1){
            if (offset > 0){
                offset = offset - 1;
            }
            y = 1;
        }

        //below the bottom text row: stay on it and scroll down a line, if there is one below
        if (y > max_text_row){
            if (offset + max_text_row < kv){
                offset = offset + 1;
            }
            y = max_text_row;
        }

        //past the file's last line: put the cursor back on it (no lines = nothing to stop at)
        if (offset + y > kv && kv > 0){
            y = kv - offset;
        }

        if (kv > 0){
            //the file line under the cursor now (the key may have moved it)
            
            int new_line = offset + y - 1;
            const std::string& text = hold_lines[new_line];
            int new_length = static_cast<int>(text.size());

            //modified = the lines no longer match the copy taken at the start
            if (og_vec != hold_lines) edit = true;
            else edit = false;

            //snap to the end of a shorter line
            if (c_index > new_length) c_index = new_length;
            if (c_index < 0) c_index = 0;

            //a different line starts unshifted, which also puts the line we left back at column 0
            if (new_line != line) coffset = 0;
            
            //the screen column the cursor's character sits at, as if the line weren't shifted
            int rcol = render_col(text, c_index);
            //cursor off the left edge: shift back so it's in the first column
            if (rcol < coffset) coffset = rcol;
            //cursor off the right edge: shift just enough that it's in the last column
            if (rcol >= coffset + max_col) coffset = rcol - max_col + 1;

            //terminal column (from 1) = where it would be, minus how far the line is shifted
            x = 1 + rcol - coffset;

            //reset line to after keys pressed
            line = offset + y - 1;
        }
        else{
            c_index = 0;
            coffset = 0;
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
            e_obj.file_name = argv[1];
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
                e_obj.hold_lines.push_back(row);
            }
        }
        else if (argc <= 1) e_obj.file_name = "no name";
        //raw mode is on for as long as obj exists, so keyboard() must run inside this block
        Raw_Mode obj;

        e_obj.keyboard();
        //leaving try destroys obj first (terminal restored), then e_obj, whose vector frees every line
    }
    //any throw above lands here; if obj was created, its destructor has already restored the terminal
    catch (const std::exception& e){
        std::cout << e.what() << strerror(errno) << "\n";
        return 1;
    }
    return 0;
}
