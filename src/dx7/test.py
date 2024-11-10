import dx7

if __name__ == "__main__":
    patch_number = 324 
    midi_note = 60
    velocity = 99
    samples = 44100 * 10 
    keyup_sample = 44100 * 5 # when to let the key up
    data = dx7.render(patch_number, midi_note, velocity, samples, keyup_sample)
    print( "Sample size: {}".format(len(data)))

