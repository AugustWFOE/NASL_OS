#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>




int main(int argc, char *argv[]) {


    if(argc < 2 ){
        printf("Please provide port number");
        return 1;
    };

    uint16_t port_number = (uint16_t)atoi(argv[1]); //Perhaps this should be validated





    //inputStore(input);

    //inputHandler(inputStore.getInput());

    //decoder(inputHandler.getHas(), inputHandler.getStart(), inputHandler.getEnd());

    //sender(decoder.getAnswer());


    // while(1){
    //     //Program probably runs inside here

        
    // }

    return 0;
}


