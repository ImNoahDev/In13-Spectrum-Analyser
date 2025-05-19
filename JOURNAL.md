Total: 4.5 hours

## Sunday 18 May (2hr)

I started to do initial research and then I begun to build a schematic. 

![image.png](/PCB/Images/image.png)

I started by adding a USB-C port. This port has the 5v input and ground. Pins CC1 and CC2 are both connected to ground with a 5.1k resistor. This allows me to tell the power source that I am requesting 5v power as a legacy device. 

I then added a decoupling capacitor between 5v and ground to smooth out the 5v supply. I also added a 0.1uF ceramic capacitor to perform High frequency decoupling on the power input. I added a resettable PTC fuse and Schottky diode to perform protection on the input and reverse polarity protection. I used a Schottky diode to minimise voltage drop.

I then added a 3.3v regulator to create a 3.3v rail for the ESP32 that will process the audio input. I chose a low dropout regulator and connected the inputs to my 5v system. I added a 10uF input capacitor to the input and a 22uF output capacitor for voltage smoothing. I then added 0.1uF ceramic caps to the input and output to perform high-frequency decoupling.

## Monday 19 May (2.5hr)

I researched how to spit my audio output into different channels. I ended up choosing a MSGEQ7 chip which splits the input audio into 7 bands.

![image.png](/PCB/Images/image-1.png)

I start by taking the output from my audio jack and adding a coupling capacitor in serices to remove and DC offset from the audio signal, this is because the MSGEQ7 expects an AC audio signal. My design expects a line level input from an audio jack. 

The reset and strope pins of the MSGEQ7 are connected to labels to be controlled by the ESP32. The output of the MSGEQ7 get given to an ADC on the ESP32. An external capacitor has been added for the oscilator in the MSGEQ7. An output filter capacitor has been added to provide stability to the output and a decoupling capacitor has been added to decouple to power supply. 