## Virtual Desktop Bar
This is an applet, a.k.a. plasmoid, for KDE Plasma panel which lets you switch between virtual desktops and also invoke some common actions to dynamically manage them in a convenient way. Those actions can be accessed through applet's context menu or user-defined global keyboard shortcuts.

The plasmoid displays virtual desktop entries as text labels with their names and optionally prepended numbers. That means there's no icons or window previews like in the Plasma's default pager applet. The intention is to keep it simple (and visually configurable in the future).

### Features
* switching to a virtual desktop
* creating a new virtual desktop
* removing last virtual desktop
* removing current virtual desktop
* moving current virtual desktop to left
* moving current virtual desktop to right
* renaming current virtual desktop

### Preview
![](preview.gif)

### Installation
In order to install this applet you have to compile it. There are, however, some required libraries which must be installed, because the building process is dependent on them.

For example, following packages should be enough to build the applet on openSUSE:
* cmake
* extra-cmake-modules
* gcc-c++
* libQt5Core-devel
* libQt5Widgets-devel
* libQt5DBus-devel
* libqt5-qtx11extras-devel
* kcoreaddons-devel
* kconfigwidgets-devel
* kwindowsystem-devel
* kguiaddons-devel

Package names may differ between distros, so it's necessary to find their counterparts supplied by a given distro.

The actual compilation and installation should be invoked as follows:
```
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release ..
make
sudo make install
```
### Tiling scripts compatibility
There are several tiling scripts for KWin which may be popular among people who use virtual desktops. Those scripts were designed to work with static virtual desktops or use their own abstraction layer to keep track of them. That means there is a problem with removing a virtual desktop or moving it to left or right while using those scripts, as it may result in tiling being messed up and broken.

If you intend not to remove virtual desktops or move them to left or right through this applet, then you should be fine with any of available tiling scripts. But if you want to take advantage of dynamic virtual desktop management and use these features, then you might want to try a [patched version of faho's KWin tiling script](https://github.com/wsdfhjxc/kwin-tiling/tree/refresh-tiles) which should behave more or less correctly in such cases.
