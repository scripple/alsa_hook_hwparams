# alsa_hook_hwparams
This is an alsa hook to let alsa pcm hardware call programs when a pcm device is first opened, and when hw_params are about to be changed or released.

I created it mainly to work with the loopback device to let the capture end connect/disconnect with changing hw_params so the playback end can set the parameters freely.  This is necessary because once one end of the loopback device is configured the other end cannot change the hw parameters.  alsa has a pcm_notify option for the loopback module that is supposed to handle this but it's broken.  Hopefully they fix it someday and this hook will no longer be necessary.  (Or maybe someone will find other uses for a callback that publishes the hw_params.)

The hook is called by alsa at three points in the handling of a pcm device.

1) When the device is opened.
2) When snd_pcm_hw_params is called on the device.
3) When snd_pcm_hw_free is called on the device.  (Part of snd_pcm_close too.)

The hook executes the command specified by "opencmd" for case 2.
The hook executes the command specified by "closecmd" for cases 1 and 3.

The commands will execute as the user running the program that opened the pcm device via this hook.

"closecmd" is called exactly as the parameter is specified in the asound.conf file.
"opencmd" is called as specified with three additional parameters appended to the call.  Those are "format rate channels".

You can specify any command you wish in the format of "path/command args".

Unfortunately the hook itself is not supplied the hw_params when called inside of alsa.  A snd_pcm_t handle is passed to the hook, but snd_pcm_hw_params_current fails as the device is not setup yet.  So this program actually parses the proc file system to obtain the parameters.  This means that whatever device is wrapped must actually populate the proc file system.  Hardware devices and the loopback device do, but for example the alsa file device does not.

This limitation could be overcome by building this hook alongside the full alsa lib as the data is available internally, but it seemed simpler to have it be buildable just against the standard alsa user space environment.

I've asked alsa-devel if there is another way to get the parameters and even offered a patch to make the hw_params available but did not receive a response.

For the loopback scenario the capture program should respond to the close command by closing its connection to the loopback device and respond to the open command by connecting to the loopback device with the specified parameters.  Sadly for this scenario point 1) above is too late to allow the play program to set the hw_params as if the capture program still had the loopback device open at that point the playback program will have been told by alsa that the device only supports the parameters currently set by the capture end.  So the point of calling close during 1) is that the playback program will attempt to open the device with its desired parameters and fail but in doing so will cause the capture program to release the loopback device.  Then if the playback program tries again it will succeed.  In terms of user interaction this typically means a problem can be cleared just be clicking play a second time.  Not perfect but better than them having to go kill the capture program manually to get things back in sync.

Provided in the hopes its helpful.  Very limited testing so use at your own risk.

To build on a debian system you need the standard c build tools and libasound2-dev.

sudo apt install libasound2-dev
make
sudo make install

Then modify the asound.conf / .asoundrc file to meet your needs based on the provided example.  Remember .asoundrc goes in the home directory to work for just one user.  asound.conf goes in /etc to provide system wide defintions.

The provided example just echos "close" and "open {format} {rate} {channels}" to stdout.  It's a nice way to see that the hook is working when used with something simple like aplay.

A simple example.

Run:
aplay -D hwparams -r 44100 -c 2 -f S16_LE < /dev/zero

Output:
close
Playing raw data 'stdin' : Signed 16 bit Little Endian, Rate 44100 Hz, Stereo
open S16_LE 44100 2

Hit ctrl-c to stop it and the output is:
^CAborted by signal Interrupt...
close

Note one other limitation of the loopback device in general is that it will NOT pass along the actual capabilities of any hardware device that will ultimately be used.  So the playback program cannot learn the rates, channels, formats, etc. in the normal alsa way when playing to the loopback device.  This hook has no effect on that limitation.
