#include <cerrno>
#include <exception>
#include <stdexcept>
#include<termios.h>
#include<unistd.h>
#include<iostream>
#include<stdio.h>
#include<stdlib.h>
#include<string>
#include<cctype>

class Capture_Input{

    private: 
    public:
    	struct termios oldt, newt;
     	bool& tracker;

      	//grab terminal attributes for POSIX standard input and nickname it oldt
      	int grab(){
     		bool OT_GRAB = tcgetattr(STDIN_FILENO, &oldt);
            if (OT_GRAB){
                //throw "Error getting terminal attributes\r\n";
                throw std::runtime_error("Error getting terminal attributes\r\n");
            }
                return 0;
        }

        //set terminal attributes
      	int set(){
      		bool set_result = tcsetattr(STDIN_FILENO, TCSANOW, &newt);	
        	if (set_result){
         		//throw "Error setting terminal attributes\r\n";
         		throw std::runtime_error("Error setting terminal attributes\r\n");
        	}	
           	return 0;            
        }

        int return_terminal(){
        	bool setback = tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            if (setback){
                perror("Failure destructing");
            }
            return 0;
        }
        
        int keyboard(){
            char ch;

            std::cout << "Press any keys, press (CTRL-Q) to quit\r\n";   
            while (true){
                ssize_t n = read(STDIN_FILENO, &ch, 1); 
                if(n == -1){
               		perror("Error reading input");
                 	return -1;
                }
                if (n == 0){
                    return EOF;
                }   
                else if (std::isprint(static_cast<unsigned char>(ch))){
                  	std::cout << "You pressed: [" << ch << "]-" << "Byte Number: " << static_cast<int>(ch) << "\r\n";
                }
                else if (std::iscntrl(static_cast<unsigned char>(ch))){
                  	std::cout << "Unprintable character-" << "Byte Number: " << static_cast<int>(ch) << "\r\n";
                }
                if (ch == '\x11') break; 
            }
            return 0;            
        }
        
        Capture_Input(bool& tracker_v) : tracker(tracker_v){
            tracker  = false;

            grab();
            newt = oldt;
            cfmakeraw(&newt);
            std::cout << "Construct executed\r\n";
            set();
            keyboard();
        } 

        ~Capture_Input(){
            tracker  = true;
            return_terminal();
        }       
};        


int main(){
	
    bool destroyed = false;
    {
    	try{
        	Capture_Input obj(destroyed);
     	}

      	/* 
     	catch(const char* msg){
          	std::cout << "Error: " << msg <<"\r\n";
      	}
       	*/
      	catch(const std::exception& e){
       		std::cout << e.what() <<"\r\n";
         	return errno;
         	return 1;
       	}  
    }
    
    if(destroyed){
        std::cout << "Destruct executed = " << std::boolalpha << destroyed << "\r\n";
    }
}
