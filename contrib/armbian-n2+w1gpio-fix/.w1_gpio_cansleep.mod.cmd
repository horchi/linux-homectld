savedcmd_w1_gpio_cansleep.mod := printf '%s\n'   w1_gpio_cansleep.o | awk '!x[$$0]++ { print("./"$$0) }' > w1_gpio_cansleep.mod
