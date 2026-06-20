#include "common.h"
#include <string.h>
#include <stdlib.h>

char **tokenise(char *str){
  char *toks[] = (char**) malloc(sizeof(char*) * (MAX_TOKENS + 1));
  toks[0] = strtok(str, " ");
  int i = 1;
  
  do {
    toks[i] = strtok(NULL, " ");
    ++i;
  }
  while(toks[i-1] != NULL && i < MAX_TOKENS);

  if(i == MAX_TOKENS) toks[i] = NULL;
  return toks;
}

void init_routing_table(Device devices[]){
  for (int i = 0; i < MAX_DEVICES; ++i) {
    devices[i].is_active = 0;
  }
}

int find_device_index(int logical_id, Device devices[]){
  for (int i = 0; i < MAX_DEVICES; ++i){
    if (devices[i].is_active &&
      devices[i].logical_id == logical_id)
    {
      return i;
    }
  }
  return -1;
}