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

struct Editor{
    int x = 1;
    int y = 1;
    int max_row;
    int max_col;
    
    //draw one full screen: clear, the welcome message, a ~ on every row, then the cursor at (y, x)
    void window_size(){
        struct winsize w;

        //ask the kernel for the window size (rows and columns) into w
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1){
            std::cout << "\033[2J\033[H" << std::flush;
            throw std::runtime_error("Error getting terminal window size\n");
        }
        
        max_row = w.ws_row;
        max_col = w.ws_col;

        
        //the message sits two thirds of the way down
        int msg_row = (max_row / 3);

        std::ostringstream message;
        message << "WELCOME TO BLAISE TERMINAL";
        std::string welcome = message.str();
        //center = middle of the screen minus half the message; done in int so it can go negative safely
        int Swelcome = static_cast<int>(welcome.size());
        int msg_col = (max_col / 2) - (Swelcome / 2);
        //cut the message to the screen width so it never wraps
        std::string sub = welcome.substr(0,max_col);

        //columns start at 1, so never send a column below that
        if (msg_col < 1){
            msg_col = 1;
        }
        //clear the screen, jump to (msg_row, msg_col), print the message, then go back to the top left
        std::cout << "\033[2J\033[" << msg_row << ";" << msg_col << "H" << sub << "\r";
        std::cout << "\033[H" << std::flush;
        //one ~ per row; no newline after the last row, or the screen would scroll up
        for (int i = 0; i < w.ws_row; ++i){
            std::cout << "~\r";
            if (i < w.ws_row -1){
                std::cout << "\n";
            }
        }
        std::cout << "\033[" << y << ";" << x << "H" << std::flush;
    }
    //the editor's main loop: redraw, wait for one key, repeat until ctrl-q (byte 17)
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
    };
    Keys read_keys(){
    	char seq[3];
    	char ch;
        ssize_t n = 0;
        while (n == 0){
            n = read(STDIN_FILENO, &ch, 1);
        }
     	if(n == -1){
      		throw std::runtime_error("Error reading input\n");
		}
        if (ch == ctrl_q) return Keys::quit;
        if (ch == ESC){
        	
         	ssize_t s = read(STDIN_FILENO, seq, 3);
	        if (s == 0){
                return Keys::other;
            }
            if(s == -1){
	         		throw std::runtime_error("Error reading input\n");
			}
            if (seq[0] == '['){
                if (seq[1] == 'A') return Keys::arrow_up;
                if (seq[1] == 'B') return Keys::arrow_down;
                if (seq[1] == 'C') return Keys::arrow_right;
                if (seq[1] == 'D') return Keys::arrow_left;
                if (seq[1] == '5'){
                    if (seq[2] == '~') return Keys::pg_up;
                }
                if (seq[1] == '6'){
                    if (seq[2] == '~') return Keys::pg_down;
                }
                if (seq[1] == 'H') return Keys::home;
                if (seq[1] == 'F') return Keys::end;
            }
            else{
            	return Keys::other;
            }
        }
    
        if (ch != ESC) return Keys::other;
        return Keys::other;
    }

    int keyboard(){
	    
		window_size();
	    while (true){
	    			
	        
	       		
				
	        
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
                }
						
                if (y > max_row){
                    y = max_row;
                }
                if (y < 1){
                        y = 1;
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

int main(){
    try{
        //raw mode is on for as long as obj exists, so keyboard() must run inside this block
        Raw_Mode obj;
        Editor e_obj;
        e_obj.keyboard();
    }
    //any throw above lands here; obj's destructor has already restored the terminal
    catch(const std::exception& e){
        std::cout << e.what() << strerror(errno) << "\n";
        return 1;
    }
}
