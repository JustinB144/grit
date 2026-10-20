#include<cerrno>
#include<climits>
#include<exception>
#include<stdexcept>
#include<sys/types.h>
#include<termios.h>
#include<unistd.h>
#include<iostream>
#include<stdio.h>
#include<stdlib.h>
#include<cctype>
#include<sys/ioctl.h>
#include<sstream>
#include<cstring>
class Capture_Input{

    private:
    public:

        //oldt = the original terminal settings (kept so we can restore them), newt = the raw-mode copy we build
    	struct termios oldt, newt;


      	int grab(){
      	//grab terminal attributes for POSIX standard input into address oldt
     		bool OT_GRAB = tcgetattr(STDIN_FILENO, &oldt);
            if (OT_GRAB){
                throw std::runtime_error("Error getting terminal attributes\n");
            }
                return 0;
        }

        //set terminal attributes
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

        void window_size(){
           	struct winsize w;



           	//bool get_wsize = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);


            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1){
                std::cout << "\033[2J\033[H" << std::flush;
               	throw std::runtime_error("Error getting terminal window size\n");
            }

            int max_row = w.ws_row;
            int max_col = w.ws_col;
            int msg_row = (max_row / 3) * 2;


            std::ostringstream message;
            message << "BLAISE TERMINAL";
            std::string welcome = message.str();
            int Swelcome = static_cast<int>(welcome.size());
            int msg_col = (max_col / 2) - (Swelcome / 2);
            std::string sub = welcome.substr(0,max_col);
            
            if (msg_col < 1){
                msg_col = 1;
            }
                std::cout << "\033[2J\033[" << msg_row << ";" << msg_col << "H" << sub << "\r";
                std::cout << "\033[H" << std::flush;
             	for (int i = 0; i < w.ws_row; ++i){
                 	std::cout << "~\r";
                  	if (i < w.ws_row -1){
                    	std::cout << "\n";
                    }
                }
            std::cout << "\033[H" << std::flush;
        }
        //read one byte at a time and print its value, until ctrl-q (byte 17)
        int keyboard(){
            char ch;

            while (true){
                window_size();
                //read 1 byte from standard input into ch; n = how many bytes arrived (-1 error, 0 end of input)
                ssize_t n = read(STDIN_FILENO, &ch, 1);

                if(n == -1){
               		throw std::runtime_error("Error reading input\n");
                }
                if (n == 0){
                    return EOF;
                }
                if (ch == '\x11'){
                    std::cout << "\033[2J\033[H" << std::flush;
                    break;
                }
            }
            return 0;
        }

        //runs when the object is created: save the settings, switch to raw mode, start reading keys
        Capture_Input(){

            grab();
            //copy the original settings, then turn the copy into raw mode
            newt = oldt;
            cfmakeraw(&newt);
            set();

            //Capture_Input(const Capture_Input& other);
        }
        //runs automatically when obj goes out of scope: restore the terminal
        ~Capture_Input(){
            return_terminal();

        }

        Capture_Input(const Capture_Input& ) = delete;

        Capture_Input& operator=(const Capture_Input&) = delete;

};
int main(){

    
    try{
        Capture_Input obj;

        obj.keyboard();
    }
      	catch(const std::exception& e){
       		std::cout << e.what() << strerror(errno) << "\n";
         	return 1;
       	}
}


