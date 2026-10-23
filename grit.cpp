#include <climits>
#include<vector>
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
constexpr char TAB = '\x09';
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
//the editor's state (cursor, screen size, scroll position, the file's lines) and everything that uses it
struct Editor{
    //cursor position on the screen; both count from 1, like the terminal
    int x = 1;
    int y = 1;
    //screen size, filled in by window_size() on every redraw
    int max_row;
    int max_col;
    //the file line shown on the top row (0 = the file's first line)
    std::vector<std::string> hold_lines;
    //the file line under the cursor is offset + y - 1
    int offset = 0;
    int coffset = 0;
    int c_index = 0;
    int kv = 0;
    int file_line = 0;
    int text_length = 0;
    int screen_col = 0;
    int fline = 0;
    int tlength = 0;
    
    //draw one full screen: clear, one file line (or ~ past the end) per row,
    //the welcome message if no lines were loaded, then the cursor at (y, x)
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
            if (offset + i < wv && wv > 0){
               		//cut the line to the screen width so it never wraps onto the next row
            		std::string lines_string = hold_lines.at(offset + i);
              		std::string vsub = lines_string.substr(0, max_col);
                		
              
              		if (vsub.size() < lines_string.size()) {
                		vsub = lines_string.substr(0, max_col - 1);
                 		std::cout << vsub << ">";
               		}
          		    else {
             			std::cout << vsub;
             		} 
                
                
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
            //the rest of an escape sequence arrives right after the ESC; take only the first 2 bytes
            //(a letter ends the key; a digit means more bytes follow), s = how many came (0 = a lone Esc press)
            //reading only what this key needs leaves the next key's bytes alone when keys pile up
            ssize_t s = read(STDIN_FILENO, seq, 2);
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
                //Page Up = ESC [ 5 ~, Page Down = ESC [ 6 ~: read the last byte into seq[2]
                if (seq[1] == '5' || seq[1] == '6'){
                    ssize_t s1 = read(STDIN_FILENO, &seq[2], 1);
                    if (s1 == -1){
                        throw std::runtime_error("Error reading input\n");
                    }
                    if (s1 < 1) return Keys::other;
                    //seq[1] says which key it is
                    if (seq[1] == '5' && seq[2] == '~') return Keys::pg_up;
                    if (seq[1] == '6' && seq[2] == '~') return Keys::pg_down;
                }
            }
            else{
                return Keys::other;
            }
        }

        return Keys::other;
    }
    int columns(int line ){
   		int col = 0;
     	
     	for (int i = 0; i < c_index; ++i){
      		std::string characters = hold_lines[file_line];
      		if (characters[i] == '\x09'){
        		col = col + (4 - (col % 4));
            }
            else{
                ++col;
      	    }
        	
        }
        return col;
    }
    //the editor's main loop: wait for a key, update the cursor and scroll, redraw; until ctrl-q
    int keyboard(){
        window_size();
        kv = static_cast<int>(hold_lines.size());
        
        while (true){
        	
         	if (kv > 0){
        		file_line = offset + y - 1;
         		text_length = static_cast<int>(hold_lines[file_line].size());

          	}
            //each key only changes the state (x, y, offset); the checks below keep it valid
            switch(read_keys()){
            
                case Keys::arrow_up : y--; break;
                case Keys::arrow_down : y++; break;
                case Keys::arrow_right :
                                        if (c_index < text_length){
                                        	++c_index;
                                         
                                        }
                                        else if (file_line < kv - 1 && kv > 0){
                                            y++;
                                            c_index = 0;
                                        }
                                        break;
                case Keys::arrow_left : 
                                            if (c_index > 0) c_index--;
                                            else if (file_line > 0 && kv > 0){
                                                y--;
                                                c_index = INT_MAX;
                                            }
                                            break;
                case Keys::quit : std::cout << "\033[2J\033[H" << std::flush; return 1;
                case Keys::other : break;
                case Keys::pg_up : y = 1; break;
                case Keys::pg_down : y = max_row; break;
                case Keys::home : c_index = 0; break;
                case Keys::end : c_index = INT_MAX; break;
                case Keys::ctrl_end :
                    //show the last screenful of the file (or from the top, if it all fits), cursor on the bottom row
                    offset = kv - max_row;
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
                if (offset + max_row < kv){
                    offset = offset + 1;
                }
                y = max_row;
            }

            //past the file's last line: put the cursor back on it (no lines = nothing to stop at)
            if (offset + y > kv && kv > 0){
                y = kv - offset;
            }
                
            if(kv > 0){
            fline = offset + y - 1;
            tlength = static_cast<int>(hold_lines[fline].size());
                if (c_index > tlength) c_index = tlength;
            x = 1 + columns(file_line);
            }
            else if (kv == 0){
            	c_index = 0;
            }    
            if (c_index < 0){
            	c_index = 0;
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
                e_obj.hold_lines.push_back(row);
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
