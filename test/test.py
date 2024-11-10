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