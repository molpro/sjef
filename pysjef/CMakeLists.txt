cmake_minimum_required(VERSION 3.14)
project(sjef-install)

# Download and compile ``sjef``
  
include(FetchContent)
 
FetchContent_Declare(
        sjef
        GIT_REPOSITORY https://gitlab.com/molpro/sjef.git
        GIT_TAG 1.22.1
)

FetchContent_MakeAvailable(sjef)

