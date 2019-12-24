#! /bin/bash -u

cat $1 | ./valgrind.sh > resultados.txt 2> errores.txt

errores=$(cat errores.txt | wc -l)
if [ $errores -gt 0  ]
then
	echo "Ha habido algun error durante la ejecucion, mostrando fichero 'errores.txt'"
	echo
	cat errores.txt
	echo
else
	echo "No ha habido errores durante la ejecucion"
	echo
fi

echo "Resultados de la ejecucion en el fichero -----> 'resultados.txt'"
echo

echo "Se muestra a continuacion los resultados de 'valgrind.sh', para mas informacion consulta el fichero 'valgrind.out'"
echo
cat valgrind.out | tail -n 12 | head -n 8
exit
