ultra96-fractal
===============

This is an Hardware-accelerated [Julia set][julia set] explorer running on [Ultra96][ultra96]. The current implementation can generate 1920x1080 pixels image at 16 frames per seconds.

![Picture][picture]

Video: <https://twitter.com/myon___/status/1163835624710795264>

## Try it on your Ultra96

1. Download `BOOT.BIN` and `image.ub` from the [latest release page][latest release], copy these onto FAT32 formatted microSD card and insert it into the Ultra96.
2. Connect the Ultra96 to a monitor and power supply.
3. (optional but recomended) Connect the gamepad (tested with [Logitech Gamepad F310][f310]) to the Ultra96.
4. Press the power button.

## How to build

Required Tools:

- [Vivado Design Suite - HLx Editions][vivado] (2019.2)
- [PetaLinux Tools][petalinux] (2019.2)

1. Clone repository

        $ git clone --recursive https://github.com/Tosainu/ultra96-fractal.git
        $ cd ultra96-fractal

2. Import Vivado project and generate Bitstream

        $ vivado -mode tcl -source fractal.tcl

        Vivado% launch_runs impl_1 -to_step write_bitstream -jobs 32
        Vivado% wait_on_run impl_1
        Vivado% write_hw_platform -fixed -include_bit vivado_project/system_wrapper.xsa
        Vivado% q

3. Build PetaLinux project

        $ cd petalinux_project
        $ petalinux-config --silentconfig --get-hw-description=../vivado_project
        $ petalinux-build
        $ petalinux-package --boot \
            --fsbl images/linux/zynqmp_fsbl.elf \
            --fpga project-spec/hw-description/system_wrapper.bit \
            --pmufw images/linux/pmufw.elf \
            --u-boot

## How it works

:construction: TODO

## License

Distributed under the [MIT license](https://github.com/Tosainu/ultra96-fractal/blob/master/LICENSE) except these files.

- [petalinux_project/project-spec/meta-user/recipes-modules/kernel-module-fractal/*](https://github.com/Tosainu/ultra96-fractal/tree/master/petalinux_project/project-spec/meta-user/recipes-modules/kernel-module-fractal)
    - [GNU General Public License v2.0](https://github.com/Tosainu/ultra96-fractal/blob/master/petalinux_project/project-spec/meta-user/recipes-modules/kernel-module-fractal/files/COPYING)

## See also

- Blog posts
    - (Japanese) [Ultra96 で Julia set をぐりぐり動かせるやつを作った | Tosainu Lab](https://blog.myon.info/entry/2019/05/15/ultra96-julia-set-explorer/)
- Slides
    - (Japanese) [FPGA/CPU混載型SoCを用いたソフト・ハード協調システムの開発事例](https://l.myon.info/reconf-1905/) (May 10th, 2019 @ [RECONF](https://www.ieice.org/ken/program/index.php?tgs_regid=d5bf7fa40ee2a058a5b70087cf04a30745112da894892ad0e7f2fe4f459be8c4&tgid=IEICE-RECONF))

[julia set]: https://en.wikipedia.org/wiki/Julia_set
[ultra96]: https://www.96boards.org/product/ultra96/
[picture]: https://github.com/Tosainu/ultra96-fractal/blob/master/images/IMG_20190827_143055-3.jpg
[video]: https://github.com/Tosainu/ultra96-fractal/blob/master/images/IMG_20190827_143055-3.jpg
[latest release]: https://github.com/Tosainu/ultra96-fractal/releases/tag/v0.1.1
[f310]: https://www.logitechg.com/en-roeu/products/gamepads/f310-gamepad.html
[vivado]: https://www.xilinx.com/products/design-tools/vivado.html
[petalinux]: https://www.xilinx.com/products/design-tools/embedded-software/petalinux-sdk.html
