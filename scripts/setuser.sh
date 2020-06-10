

user="$1"
md5=`echo -n "$2"|md5sum | sed s/" .*"/""/g`

echo $user $md5

mysql -h gate -u pool -ppool -Dpool -e " \
  update config set value = '$user' where owner = 'poold' and name = 'user';"

mysql -h gate -u pool -ppool -Dpool -e " \
  update config set value = '$md5' where owner = 'poold' and name = 'passwd';"
