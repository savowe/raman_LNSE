import talisestools as tt 
import numpy as np
from matplotlib import pyplot as plt

data_psi1 = tt.readall(1)

den_1 = np.abs(data_psi1["wavefunction"]**2)
max_density = np.max(den_1)

t = data_psi1["t"]
x = np.linspace(data_psi1["xMin"], data_psi1["xMax"], data_psi1["nDimX"])
y = np.linspace(data_psi1["yMin"], data_psi1["yMax"], data_psi1["nDimY"])

## Create animated gif
import gif
@gif.frame
def plot(den1, x, y, t, max_density):
    fig, ax = plt.subplots(1)
    ax.pcolormesh(x, y, den1, vmax = max_density)
    ax.set_title(r"t = {:10.0f} $\mu$s".format(t))
    ax.grid()
    plt.tight_layout()

frames = []
for i in range(0,len(t)):
    frame = plot(den_1[:,:,i], x, y, t[i], max_density)
    frames.append(frame)
    print("Generated plot "+str(i)+"/"+str(len(t)-1))

gif.save(frames, "eval.gif", duration=100)