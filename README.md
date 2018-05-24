# Pursuit.gg

OBS Filter Plugin to output frame captures from OBS for the Pursuit.gg client to upload

## Requirements
  - Currently only building for 64-bit Windows is tested
  - CMake 3.0 or higher - [cmake.org](https://cmake.org)
  - Visual Studio 2017, include C++ in install
  - Built OBS Studio Libraries
      [install instructions](https://github.com/jp9000/obs-studio/wiki/Install-Instructions)

## Building
  - From the Developer Command Prompt for VS 2017, navigate to the libjpeg folder and create the jpeg Visual Studio Solution:

  ```
  nmake -f makefile.vs setup-v15
  ```

  - Open the ```jpeg.sln``` file that was created in Visual Studio.
  - Use the configuration manager to set the platform to ```x64```, may have to create a new x64 platform copying settings from Win32.
  - Run ```Build```. This will build the jpeg.lib object file library under x64/Release which we will link to the OBS Plugin. 

  - Create a build folder within the obs-frame-capture-filter folder. Example: ```obs-frame-capture-filter/build```
  - Run ```cmake-gui``` from the command prompt in the obs-frame-capture-filter folder
  - Set ```Where is the source code:``` to the obs-frame-capture-filter folder, and ```Where to build the binaries:``` to the build folder you just made.
  - Hit the ```CONFIGURE``` button and choose ```Visual Studio 15 2017 Win64``` as the generator.
  - Fill in the ```PATH_OBS_STUDIO``` variable with the location of the obs-studio folder that has the built libraries
  - Hit ```CONFIGURE``` again and it should succeed.
  - Hit ```GENERATE```.
  - Hit ```OPEN PROJECT```.
  - Select ```RelWithDebInfo``` and ```x64``` in the configuration manager.
  - Run ```Build```.
  - Under ```RelWithDebInfo``` in your build folder you will now have a .dll file which can be added to obs by copying to ```obs-plugins\64bit``` within the installed obs files.

 
### Notice
  The libjpeg folder is an unmodified copy of version 9c of the JPEG library written by The Independent JPEG Group and downloaded from [www.ijg.org](http://www.ijg.org).
