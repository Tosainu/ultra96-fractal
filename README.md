ultra96-fractal
===============

This is an Hardware-accelerated [Julia set][julia set] explorer running on [Ultra96][ultra96]. The current implementation can generate 1920x1080 pixels image at 16 frames per seconds.

![Picture][picture]

Video: <https://twitter.com/myon___/status/1163835624710795264>

## Try it on your Ultra96

1. Download `ultra96v2.zip` from the [latest release page][latest release], extract the files (`BOOT.BIN`, `boot.scr`, `image.ub`), and copy these onto FAT32 formatted microSD card.
2. Insert the microSD card into the Ultra96 microSD card slot.
3. Connect the Ultra96 to a monitor.
4. (optional) Connect a gamepad (e.g. [Logitech Gamepad F310][f310]) to the Ultra96.
5. Connect the power supply to the Ultra96.
6. Press the power button.

## How to build

Required Tools:

- [Vivado Design Suite - HLx Editions][vivado] (2022.1)
- [PetaLinux Tools][petalinux] (2022.1)

1. Clone repository

        $ git clone --recursive https://github.com/Tosainu/ultra96-fractal.git
        $ cd ultra96-fractal

2. Import Vivado project and generate Bitstream

        $ vivado -mode tcl -source fractal.tcl -tclargs

        Vivado% launch_runs impl_1 -to_step write_bitstream -jobs 32
        Vivado% wait_on_run impl_1
        Vivado% write_hw_platform -fixed -include_bit vivado_project/system_wrapper.xsa
        Vivado% q

3. Build PetaLinux project

        $ cd petalinux_project
        $ petalinux-config --silentconfig --get-hw-description=../vivado_project
        $ petalinux-build
        $ petalinux-package --boot --fpga --u-boot

4. Copy generated files to microSD card

        $ cp image/linux/{BOOT.BIN,boot.scr,image.ub} /path/to/sd/card

## How it works

![Block diagram][block diagram]

:construction: TODO

## License

Distributed under the [MIT license](https://github.com/Tosainu/ultra96-fractal/blob/master/LICENSE) except these files.

- [petalinux_project/project-spec/meta-user/recipes-modules/kernel-module-fractal/*](https://github.com/Tosainu/ultra96-fractal/tree/master/petalinux_project/project-spec/meta-user/recipes-modules/kernel-module-fractal)
    - [GNU General Public License v2.0](https://github.com/Tosainu/ultra96-fractal/blob/master/petalinux_project/project-spec/meta-user/recipes-modules/kernel-module-fractal/files/COPYING)

## See also

- Blog posts
    - [Ultra96 で Julia set をぐりぐり動かせるやつを作った | Tosainu Lab](https://blog.myon.info/entry/2019/05/15/ultra96-julia-set-explorer/)
    - [Ultra96 で Julia set をぐりぐり動かせるやつをもう少し強くした | Tosainu Lab](https://blog.myon.info/entry/2019/08/29/ultra96-julia-set-explorer-2/)
- Slides
    - [Julia set explorer を支える技術 -dma-buf によるデバイスドライバ間のバッファ共有-](https://l.myon.info/kernelvm-kansai-10) (Feb. 8th, 2020 - [カーネル/VM探検隊@関西 10回目](https://connpass.com/event/161201/))

[julia set]: https://en.wikipedia.org/wiki/Julia_set
[ultra96]: https://www.96boards.org/product/ultra96/
[picture]: https://github.com/Tosainu/ultra96-fractal/blob/master/images/IMG_20190827_143055-3.jpg
[block diagram]: https://github.com/Tosainu/ultra96-fractal/blob/master/images/block.svg
[latest release]: https://github.com/Tosainu/ultra96-fractal/releases/latest
[f310]: https://www.logitechg.com/en-roeu/products/gamepads/f310-gamepad.html
[vivado]: https://www.xilinx.com/products/design-tools/vivado.html
[petalinux]: https://www.xilinx.com/products/design-tools/embedded-software/petalinux-sdk.html
