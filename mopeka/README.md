
# Installation

## dependencies

###  python >= 3.3
```
export PY_ENV=/root/.venvs/homectld
export PY_ENV_BIN=${PY_ENV}/bin

apt -y install pip python3-venv python3-paho-mqtt

if [[ ! -d ${PY_ENV} ]]; then
    python3 -m venv --system-site-packages ${PY_ENV}
fi

${PY_ENV_BIN}/pip install --upgrade mopeka_pro_check
```
if you change the PY_ENV path you have to patch the Makefile too!


### Oder python
```
apt -y install pip python3-paho-mqtt
pip install --upgrade mopeka_pro_check
```

## install

```
make install
```

And adjust settings in /etc/default/mopeka2mqtt

## get the device MAC

press Sync button and call `mopeka.py -D`

## Tank heights

- 11 kg (europe) - full at 365 mm
- 5  kg (europe) - full at ~310 mm ??

# my own sensor

mac f0:87:60:83:ab:10
