# PicoScope2206B-AdLab
Code written to interface with the PicoScope 2206B MSO desktop oscilloscope for an undergrad physics advanced lab project. The project aimed at measuring the lifetime of cosmic ray muons.

The PicoScope 2206B MSO desktop oscilloscope was connected to a photo-multiplier tube, which in turn was connected to a plastic scintillating bar. when a cosmic ray muon strikes a nucleus in the bar and stops in the scintillation bar’s frame, a photon is emitted, which is then detected by the photo-multiplier tube. Shortly after, (typically a few microseconds) the muon will decay, emitting another photon that will again be collected by the photo-multiplier tube. Given a sufficiently large collection of decay times, a fairly accurate average decay time can be calculated given the muon’s exponential probability of decay with time.

The code in this project automated the detection of such collisions and subsequent decays via a peak detection algorithm. The peak to peak (and thus decay) times of such "two peak" events were then recorded to a .csv file for later analysis. In the end, a mean lifetime of 2152 ± 68 ns 95% CI was determined, which falls well within the accepted value of 2197 ns. See the included writeup for more details.

A great amount of thanks must be given to hsmistry, whose example code (https://github.com/picotech/picosdk-c-examples/blob/master/ps2000a/ps2000aCon/ps2000aCon.c) this project was built on top of. Without it, I would not have figured out PicoScope SDK and been able to complete the measurement. 
