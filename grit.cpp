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
/*
enum class arrows{
    


}
*/

struct Editor{
    int x = 1;
    int y = 1;
    int max_row;
    int max_col;
    //draw one full screen: clear, the welcome message, a ~ on every row, then cursor to the top left
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
        int msg_row = (max_row / 3) * 2;

        std::ostringstream message;
        message << "BLAISE TERMINAL";
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


    int keyboard(){
	    char ch;
	    char seq[2];
		window_size();
	    while (true){
	    	
			
	        //read 1 byte from standard input into ch; n = how many bytes arrived (-1 error, 0 end of input)
	        ssize_t n = read(STDIN_FILENO, &ch, 1);
	
	        if(n == -1){
	            throw std::runtime_error("Error reading input\n");
	        }
	        if (n == 0){
	            return EOF;
	        }
	        //ctrl-q: clear the screen so the shell comes back clean, then leave the loop
	        if (ch == '\x11'){
	            std::cout << "\033[2J\033[H" << std::flush;
	            break;
	        }
	        
	        if (ch == '\x1b'){
	            if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0){
	                if (seq[0] == '['){
	                    switch(seq[1]){
	                        case 'A' : y--; break;
	                        case 'B' : y++; break;
	                        case 'C' : x++; break;
	                        case 'D' : x--; break;
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
	            }
	        }
			
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
