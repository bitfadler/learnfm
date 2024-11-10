# learnfm

This is a fork of https://github.com/bwhitman/learnfm.
It improves upon the original work by removing warnings and improving the Python module installation.
See the original repository for additional info.

# Building

## GNU/Linux
To use this module in Python, install it locally
```
python -m venv venv
source ./venv/bin/activate
pip install .
```

## Other environments
Untested.

# Usage
As with the original repo, the module generates sample data from a patch number or data set, a midi note and velocity as well as a total number of samples and the sample number of the key-up event. The sound is being rendered as 44.1 kHz, 16 bit signed array.

## Example

The following example creates a wave file of a given patch: 

```
import dx7
import wave
from array import array

def generate_patch(patch, note) -> array:
    print( f"Exporting patch {patch} (note: {note})")
    velocity = 99
    samples = 44100 * 10 
    keyup_sample = int(samples*0.5)
    data = dx7.render(patch, note, velocity, samples, keyup_sample)
    return array("i", data)


if __name__ == "__main__":
    a = generate_patch(324, 60)

    with wave.open("output.wav", mode="wb") as wav_file:
        wav_file.setnchannels(2)
        wav_file.setsampwidth(2)
        wav_file.setframerate(44100)
        wav_file.writeframes(a)
```
