# WiringPi for Odroid

## WiringPi

Originally, WiringPi is made for using exposed pins on the Raspberry Pi boards by Gordon.

This program provides a handy command-line program as well as the library to use with the C/C++ based projects. Also it provides some helper APIs to use the peripherals like character LCD, or the I2C/SPI connected devices.

But today WiringPi is discontinued so there will be no additional features coming.

Here's the official website address of the WiringPi: <http://wiringpi.com/>

## Ported for Odroid

We have been developing WiringPi to use on the Odroid boards.

In this whole process, we have to remove some unsupported features but instead, we added more terrific features. That means, it is not the same as the original WiringPi. But most important parts are the same so you can use this program on the Odroid boards for your projects running on RaspberryPi on the fly.

If you face a trouble with using this, please contact us through the Hardkernel Odroid from. Here is the address with link: <https://forum.odroid.com>

## Installation

### Debian packaging (RECOMMENDED)

We're providing this project by using Launchpad PPA. You can easily install this with that PPA.

```bash
sudo add-apt-repository ppa:hardkernel/ppa
sudo apt install odroid-wiringpi libwiringpi2 libwiringpi-dev
```

Those three packages are,

- odroid-wiringpi: The WiringPi `gpio` command line utility
- libwiringpi2: The runtime that WiringPi requires
- libwiringpi-dev: Development libraries to allow GPIO access from C/C++ programs

### Clone this repository

Cloning this Github repository and execute `build` script.

```bash
git clone https://github.com/hardkernel/wiringPi
cd wiringPi
sudo ./build
```

Then the `gpio` command line utility and libraries will be installed.

To remove them, put the `uninstall` option when executing `build`.

```bash
sudo ./build uninstall
```

## Without root permission

In WiringPi, all of the exposed GPIO pins are controlled by editing GPIO registers directly. To make this job working, it is needed to use the `/dev/mem` device file.

But accessing `/dev/mem` directly can be really harmful to the device so that Linux restricts accessing this by the non-root user.

In some projects, the program should be run on a non-root account. So we're providing two options of using this on the normal user.

### Alternative chracter device

Generally, it is not be permitted to use a memory map without root permission. So we're providing the `/dev/gpiomem` character device that only exposed GPIO related areas.

This alternative device is included in the Hardkernel Linux kernel. So, if you are using Hardkernel's official Ubuntu image, you can use this out of the box when you're trying to control the GPIO pin without root permission.

### gpiod

#### Overview

The modern Linux systems support gpiod that a kind of layer to communicate with kerne about controlling GPIO pins.

In terms of "modern Linux systems", since Linux kernel 4.8 the GPIO sysfs interface is deprecated so users should find the other way instead of using the deprecated interface. This is why the gpiod introduced.

#### WiringPi with gpiod

In our WiringPi, for the first time if there's no root permission then it tries to find `/dev/gpiomem` exists. If there isn't that `/dev/gpiomem` device, then checks `libgpiod-dev` installed and the installed kernel version meets the requirements.

The `libgpiod-dev` library should be installed along with our WiringPi so this isn't the user concern. But the kernel version has to meet the minimum requirements written at [wiringGpiod.h](wiringPi/wiringGpiod.h). The version is from the libgpiod version that I'm developing with.

#### Limited supports

Unfortunately, gpiod is still working in progress so this doesn't support all of the features WiringPi provides currently. I'm continuously developing this by following the new libgpiod releasing.

But at least, the following functions should work with the minimum requirements.

- `digitalRead()`
- `digitalWrite()`
- `pinMode()` (Only for switching direction)
- `digitalReadByte()`
- `digitalWriteByte()`
- `pullUpDnControl()`
- `getPUPD()`

#### Before you use

Since `/dev/gpiochip*` devices are not permitted to non-root user, WiringPi for Odroid provides a [udev rules](udev/rules.d/10-odroid-periphs.rules) to make it fixed.

The rules will set the peripheral devices' gorups.

- `/dev/gpiochip*` belongs to **gpio** group
- `/dev/i2c*` and `/dev/spi*` belong to **smbus** group
- `/sys/class/pwm/pwmchip*/pwm*` belongs to **pwm** group

These udev rules will be installed if you execute the included `build` script or install it using one of the Debian packages, libwiringpi2.

But the installer doesn't add the user into those groups since the installer doesn't know who requires this access permission. So before you use this as gpiod mode, you have to set the user to be in the peripheral groups as listed above.

```bash
sudo usermod -aG gpio $USER
sudo usermod -aG smbus $USER
sudo usermod -aG pwm $USER
```

## Changelog

- [Commits](https://github.com/awesometic/wiringPi/commits)
- [`changelog` for Debian packaging](debian/changelog)

## References

- WiringPi Python Wrapper: <https://github.com/hardkernel/WiringPi2-Python>
- WiringPi introduction on Odroid Wiki: <https://wiki.odroid.com/common/application_note/gpio/wiringpi>
