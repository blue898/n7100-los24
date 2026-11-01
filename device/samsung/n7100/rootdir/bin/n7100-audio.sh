#!/vendor/bin/sh
# WM1811 routing for the "Midas" card: AIF1 playback to the speaker and headphones, the main mic
# (IN2L) to AIF1 capture. AOSP's primary audio HAL only opens card 0 / device 0 and sets no
# mixer controls, and the codec powers up with every path off. Run once by init at boot.
for i in $(seq 1 50); do [ -e /dev/snd/controlC0 ] && break; sleep 0.2; done
m() { /vendor/bin/tinymix -D 0 "$@" >/dev/null || echo "n7100-audio: tinymix $* failed" > /dev/kmsg; }

# playback: AIF1.1 -> DAC1 -> speaker mixer (+ output mixers for the headphones)
m "DAC OSR" "High Performance"
m "DAC1L Mixer AIF1.1 Switch" 1
m "DAC1R Mixer AIF1.1 Switch" 1
m "DAC1 Switch" 1 1
m "SPKL DAC1 Switch" 1
m "SPKR DAC1 Switch" 1
# the speaker mixer attenuation control is inverted: 3 = 0 dB, 0 = mute
m "Speaker Mixer Volume" 3 3
m "Speaker Boost Volume" 3 3
m "Speaker Volume" 60 60
m "Left Output Mixer DAC Switch" 1
m "Right Output Mixer DAC Switch" 1

# capture: main mic on IN2L -> MIXINL -> ADCL -> AIF1 ADC1, both channels from the left ADC
m "IN2L PGA IN2LP Switch" 1
m "IN2L PGA IN2LN Switch" 1
m "IN2L Switch" 1
m "MIXINL IN2L Switch" 1
m "MIXINL IN2L Volume" 1
m "AIF1ADC1L Mixer ADC/DMIC Switch" 1
m "AIF1ADCR Source" Left
m "AIF1ADC1 HPF Switch" 1 1

echo "n7100-audio: WM1811 routing set" > /dev/kmsg
