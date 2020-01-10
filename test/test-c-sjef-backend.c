#include "sjef-c.h"
#include <stdio.h>
#include <stdlib.h>

int main() {

  char** keys = sjef_backend_keys();
  int i;
  for (i = 0; keys[i] != NULL; ++i) {
    printf("%s\n", keys[i]);
    free(keys[i]);
  }
  free(keys);

  char projectname[] = "$TMPDIR/test-sjef-project/cproject.thingy";
  sjef_project_erase(projectname);
  sjef_project_open(projectname);

  char** backends = sjef_project_backend_names(projectname);
  for (i = 0; backends[i] != NULL; ++i) {
    printf("%s\n", backends[i]);
    free(backends[i]);
  }
  free(backends);

  printf("%s\n",sjef_backend_value(projectname,"local","host"));

  const char* empty[]={NULL};
//  sjef_backend_new("thing",empty,empty);
//
//  const char* newkeys[]={"run_command","status_command"};
//  const char* newvals[]={"true","status"};
//  sjef_backend_new("thing",newkeys,newvals);
//  system("cat ~/.sjef/backends.xml");
//  sjef_backend_remove("thing");
//  sjef_backend_remove("thing");
//  system("cat ~/.sjef/backends.xml");

}
