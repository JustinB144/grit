#include<termios.h>
#include<unistd.h>
#include<iostream>
#include<stdio.h>
#include<stdlib.h>
#include<string>

class Capture_Input{

    private: 
    public:
    	struct termios oldt, newt;
     	bool& tracker;

      	int grab(){
     		int OT_GRAB = tcgetattr(STDIN_FILENO, &oldt);
            if (OT_GRAB == -1){
                perror("Error getting terminal attributes\r\n");
                return 1;
            }
            else{
                return 0;
       	
            }
        }

      	int set(){
      		int set_result = tcsetattr(STDIN_FILENO, TCSANOW, &newt);
     		if (set_result == -1){
      			perror("Error setting terminal attributes\r\n");
                return 1;
       		}
            else{
       		    return 0;
            }
        }

        int return_terminal(){
        	int setback = tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            if (setback == -1){
                perror("Error setting terminal attributes back to original\r\n");
                return 1;
            }
            else{
                return 0;
            }
        }
        int keyboard(){
            char ch;
       

            //fix this, print number each byte
            std::cout << "Press any keys, press (q) to quit" << std::endl;   
            while (true){
                ssize_t n = read(STDIN_FILENO, &ch, 1); 
                if (n > 0){ if (ch == 'q') break;
                    std::cout << "You pressed: [" << ch << "]\r\n";     
                }   
            }
            return 0;
        } 

        
        Capture_Input(bool& tracker_v) : tracker(tracker_v){
            std::cout << "Construct executed\r\n";
        
            tracker  = false;

            grab();
            newt = oldt;
            cfmakeraw(&newt);
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
        Capture_Input obj(destroyed);
    }

    if(destroyed){
        std::cout << "Destruct executed = " << std::boolalpha << destroyed << "\r\n";
    }
}
