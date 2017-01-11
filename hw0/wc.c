#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 256

int main(int argc, char *argv[]) {
	char buffer[BUFFER_SIZE];
	int num_bytes=0, num_words=0, num_newlines=0, len=0, i;
	FILE *fp = fopen(argv[1], "r");
	
	while(fgets(buffer, BUFFER_SIZE, fp)){
		int num_spaces=0;
		int inword = 0;

		num_bytes += strlen(buffer);
		for(i=0;i<strlen(buffer);i++){
			if(buffer[i] == ' '){
				if(inword == 1){
					inword = 0;
					num_spaces += 1;
				}
			}
			else{
				inword = 1;
			}	
		}
		num_words += (strlen(buffer) > 1) ? num_spaces + 1 : 0;
		num_newlines += 1;
	}
	printf("%d %d %d\n", num_bytes, num_words, num_newlines);	
   return 0;
}
