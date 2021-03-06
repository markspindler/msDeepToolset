# msDeepToolset
msDeepToolset is a set of plugins for the compositing software [Nuke](https://www.foundry.com/products/nuke-family/nuke "Nuke on foundry.com") that work with Deep images. You can download compiled binaries of the indivdual plugins for different versions of Nuke from [my website](http://www.mark-spindler.com/tools.html "Tools on mark-spindler.com") or [Nukepedia](http://www.nukepedia.com/plugins/deep "Deep Plugins on nukepedia.com").

### msDeepBlur
msDeepBlur performs a Gaussian blur on Deep images. Be careful to keep the size of the blur small, as this node can become extremely slow to render for larger sizes!

### msDeepKeymix
msDeepKeymix has the same functionality as a regular KeyMix node, but works with deep images. The only other difference is that all channels will be mixed by the given mask, i.e. you can't limit the operation to specific channels and pipe the other channels through unchanged.

### msDeepReformat
msDeepReformat works like Nuke's regular DeepReformat, but uses a cubic filter.