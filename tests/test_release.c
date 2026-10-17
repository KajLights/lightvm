#include "release.h"
#include <stdio.h>
#include <string.h>
/* Tab-separated cases are shared with Python and TypeScript. */
int main(int argc,char **argv){
    if(lightvm_release_valid("1.2.0\n",true)||lightvm_release_valid("1",false))return 1;
    if(argc!=2)return 1;
    FILE *file=fopen(argv[1],"r");if(!file)return 1;
    char line[300];unsigned count=0;
    while(fgets(line,sizeof(line),file)){
        char *current=strtok(line,"\t"),*minimum=strtok(NULL,"\t"),*accepted=strtok(NULL,"\t\n");
        if(!current||!minimum||!accepted){fclose(file);return 1;}
        bool ok=lightvm_release_error(!strcmp(current,"null")?NULL:current,!strcmp(minimum,"null")?NULL:minimum)==NULL;
        if(ok!=(!strcmp(accepted,"true"))){fprintf(stderr,"release mismatch: %s / %s\n",current,minimum);fclose(file);return 1;}
        count++;
    }
    fclose(file);return count?0:1;
}
