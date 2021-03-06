/*
 * Shell `simplesh` (basado en el shell de xv6)
 *
 * Ampliación de Sistemas Operativos
 * Departamento de Ingeniería y Tecnología de Computadores
 * Facultad de Informática de la Universidad de Murcia
 *
 * Alumnos: HIDALGO ROGEL, JOSÉ MANUEL (G2.2)
 *          HERNÁNDEZ NAVARRO, ÓSCAR (G2.1)
 *
 * Convocatoria: FEBRERO
 */


/*
 * Ficheros de cabecera
 */


#define _POSIX_C_SOURCE 200809L /* IEEE 1003.1-2008 (véase /usr/include/features.h) */
//#define NDEBUG                /* Traduce asertos y DMACROS a 'no ops' */

#include <math.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Bibliotecas que hemos necesitado añadir para realizar las practicas

#include <sys/types.h>
#include <pwd.h>
#include <limits.h>
#include <libgen.h>
#include <signal.h>

// Biblioteca readline
#include <readline/readline.h>
#include <readline/history.h>


/******************************************************************************
 * Constantes, macros y variables globales
 ******************************************************************************/


static const char* VERSION = "0.19";

// Niveles de depuración
#define DBG_CMD   (1 << 0)
#define DBG_TRACE (1 << 1)
// . . .
static int g_dbg_level = 0;

#ifndef NDEBUG
#define DPRINTF(dbg_level, fmt, ...)                            \
    do {                                                        \
        if (dbg_level & g_dbg_level)                            \
            fprintf(stderr, "%s:%d:%s(): " fmt,                 \
                    __FILE__, __LINE__, __func__, ##__VA_ARGS__);       \
    } while ( 0 )

#define DBLOCK(dbg_level, block)                                \
    do {                                                        \
        if (dbg_level & g_dbg_level)                            \
            block;                                              \
    } while( 0 );
#else
#define DPRINTF(dbg_level, fmt, ...)
#define DBLOCK(dbg_level, block)
#endif

#define TRY(x)                                                  \
    do {                                                        \
        int __rc = (x);                                         \
        if( __rc < 0 ) {                                        \
            fprintf(stderr, "%s:%d:%s: TRY(%s) failed\n",       \
                    __FILE__, __LINE__, __func__, #x);          \
            fprintf(stderr, "ERROR: rc=%d errno=%d (%s)\n",     \
                    __rc, errno, strerror(errno));              \
            exit(EXIT_FAILURE);                                 \
        }                                                       \
    } while( 0 )


// Número máximo de argumentos de un comando
#define MAX_ARGS 16

// Funciones maximo y minimo
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

// Número máximo de procesos en segundo plano
#define MAX_2PLANO 8

// Delimitadores
static const char WHITESPACE[] = " \t\r\n\v";
// Caracteres especiales
static const char SYMBOLS[] = "<|>&;()";


/******************************************************************************
 * Funciones auxiliares
 ******************************************************************************/


// Imprime el mensaje
void info(const char *fmt, ...)
{
    va_list arg;

    fprintf(stdout, "%s: ", __FILE__);
    va_start(arg, fmt);
    vfprintf(stdout, fmt, arg);
    va_end(arg);
}


// Imprime el mensaje de error
void error(const char *fmt, ...)
{
    va_list arg;

    fprintf(stderr, "%s: ", __FILE__);
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
}


// Imprime el mensaje de error y aborta la ejecución
void panic(const char *fmt, ...)
{
    va_list arg;

    fprintf(stderr, "%s: ", __FILE__);
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);

    exit(EXIT_FAILURE);
}


// `fork()` que muestra un mensaje de error si no se puede crear el hijo
int fork_or_panic(const char* s)
{
    int pid;

    pid = fork();
    if(pid == -1)
        panic("%s failed: errno %d (%s)", s, errno, strerror(errno));
    return pid;
}


/******************************************************************************
 * Estructuras de datos `cmd`
 ******************************************************************************/


// Las estructuras `cmd` se utilizan para almacenar información que servirá a
// simplesh para ejecutar líneas de órdenes con redirecciones, tuberías, listas
// de comandos y tareas en segundo plano. El formato es el siguiente:

//     |----------+--------------+--------------|
//     | (1 byte) | ...          | ...          |
//     |----------+--------------+--------------|
//     | type     | otros campos | otros campos |
//     |----------+--------------+--------------|

// Nótese cómo las estructuras `cmd` comparten el primer campo `type` para
// identificar su tipo. A partir de él se obtiene un tipo derivado a través de
// *casting* forzado de tipo. Se consigue así polimorfismo básico en C.

// Valores del campo `type` de las estructuras de datos `cmd`
enum cmd_type { EXEC=1, REDR=2, PIPE=3, LIST=4, BACK=5, SUBS=6, INV=7 };

struct cmd { enum cmd_type type; };

// Comando con sus parámetros
struct execcmd {
    enum cmd_type type;
    char* argv[MAX_ARGS];
    char* eargv[MAX_ARGS];
    int argc;
};

// Comando con redirección
struct redrcmd {
    enum cmd_type type;
    struct cmd* cmd;
    char* file;
    char* efile;
    int flags;
    mode_t mode;
    int fd;
};

// Comandos con tubería
struct pipecmd {
    enum cmd_type type;
    struct cmd* left;
    struct cmd* right;
};

// Lista de órdenes
struct listcmd {
    enum cmd_type type;
    struct cmd* left;
    struct cmd* right;
};

// Tarea en segundo plano (background) con `&`
struct backcmd {
    enum cmd_type type;
    struct cmd* cmd;
};

// Subshell
struct subscmd {
    enum cmd_type type;
    struct cmd* cmd;
};


/******************************************************************************
 * Funciones para construir las estructuras de datos `cmd`
 ******************************************************************************/


// Construye una estructura `cmd` de tipo `EXEC`
struct cmd* execcmd(void)
{
    struct execcmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("execcmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = EXEC;

    return (struct cmd*) cmd;
}

// Construye una estructura `cmd` de tipo `REDR`
struct cmd* redrcmd(struct cmd* subcmd,
        char* file, char* efile,
        int flags, mode_t mode, int fd)
{
    struct redrcmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("redrcmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = REDR;
    cmd->cmd = subcmd;
    cmd->file = file;
    cmd->efile = efile;
    cmd->flags = flags;
    cmd->mode = mode;
    cmd->fd = fd;

    return (struct cmd*) cmd;
}

// Construye una estructura `cmd` de tipo `PIPE`
struct cmd* pipecmd(struct cmd* left, struct cmd* right)
{
    struct pipecmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("pipecmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = PIPE;
    cmd->left = left;
    cmd->right = right;

    return (struct cmd*) cmd;
}

// Construye una estructura `cmd` de tipo `LIST`
struct cmd* listcmd(struct cmd* left, struct cmd* right)
{
    struct listcmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("listcmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;

    return (struct cmd*)cmd;
}

// Construye una estructura `cmd` de tipo `BACK`
struct cmd* backcmd(struct cmd* subcmd)
{
    struct backcmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("backcmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = BACK;
    cmd->cmd = subcmd;

    return (struct cmd*)cmd;
}

// Construye una estructura `cmd` de tipo `SUB`
struct cmd* subscmd(struct cmd* subcmd)
{
    struct subscmd* cmd;

    if ((cmd = malloc(sizeof(*cmd))) == NULL)
    {
        perror("subscmd: malloc");
        exit(EXIT_FAILURE);
    }
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = SUBS;
    cmd->cmd = subcmd;

    return (struct cmd*) cmd;
}


/******************************************************************************
 * Implementacion de comandos internos y manejador (Boletin 2/3/4)
 ******************************************************************************/

// Declaracion previa para evitar conflictos
struct cmd* cmd;
void block_sigchld();
void unblock_sigchld();

void free_cmd(struct cmd* cmd);

char* comandosInternos[] = {
                            "cwd",
                            "exit",
                            "cd",
                            "psplit",
                            "bjobs"
                            };
const int N_INTERNOS = 5;


// Funcion interna que nos muestra el directorio actual
void run_cwd()
{
    char path[PATH_MAX];
    if(!getcwd(path, PATH_MAX)){
        perror("getcwd");
        exit(EXIT_FAILURE);
    }

    printf("cwd: %s\n", path);
}


void run_exit()
{
    free_cmd(cmd);
    free(cmd);

    exit(EXIT_SUCCESS);
}

/* Ejecuta comando interno cd. Este tiene 3 opciones.
*   - sin argumentos, cambia el directorio de trabajo a $HOME
*	- con un argumento 'dir' cambia el directorio de trabajo a 'dir'
* 	- con el argumento '-' cambia al directorio de trabajo anteriormente utilizado
*/

void run_cd(struct execcmd* ecmd)
{
    // Guarda el PATH actual
    char path[PATH_MAX];
    if(!getcwd(path, PATH_MAX)){
        perror("getcwd");
        exit(EXIT_FAILURE);
    }

    // Error: cd con mas argumentos de la cuenta
    if (ecmd->argc > 2) {
    	fprintf(stderr, "run_cd: Demasiados argumentos\n");
    } 
    // cd
    else if(ecmd->argc == 1){	
        if (setenv("OLDPWD",path,1)){
            perror("chdir (setenv)");
            exit(EXIT_FAILURE);
        }
        if(chdir(getenv("HOME"))){
            perror("chdir");
            exit(EXIT_FAILURE);
        }
    }
    // cd -
    else if(strcmp(ecmd->argv[1],"-") == 0){
        char * aux = getenv("OLDPWD");
        if(aux == NULL){
            fprintf(stderr, "run_cd: Variable OLDPWD no definida\n");
        }
        else {
	        if (setenv("OLDPWD",path,1)){
                perror("chdir (setenv)");
                exit(EXIT_FAILURE);
            }
	        if(chdir(aux)){
	            perror("chdir");
                exit(EXIT_FAILURE);
        	}
        }
    }
    // cd dir
    else {
        if (setenv("OLDPWD",path,1)){
            perror("chdir (setenv)");
            exit(EXIT_FAILURE);
        }
        if(chdir(ecmd->argv[1]))
            fprintf(stderr, "run_cd: No existe el directorio '%s'\n", ecmd->argv[1]);
    }
}

char * help_psplit(){
    return "Uso: psplit [-l NLINES] [-b NBYTES] [-s BSIZE] [-p PROCS] [FILE1] [FILE2]...\n\tOpciones:\n\t-l NLINES Número máximo de líneas por fichero.\n\t-b NBYTES Número máximo de bytes por fichero.\n\t-s BSIZE Tamaño en bytes de los bloques leídos de [FILEn] o stdin.\n\t-p PROCS Número máximo de procesos simultáneos.\n\t-h        Ayuda\n";
}

// Funcion auxiliar que hemos usado para la implementación del comando psplit
// Convierte un entero 'val' en una base 'base' en un char*, válido hasta base 16
char* itoa(int val, int base){
    static char buf[32] = {0};
    if(val == 0){
        buf[30] = '0';
        return &buf[30];
    }

    int i = 30;
    for(; val && i ; --i, val /= base)
        buf[i] = "0123456789abcdef"[val % base];
    return &buf[i+1];
    
}

// Funcion que dado un nombre de fichero 'nombre' y un entero 'indice' los concatena en un char* 'dst'
void nombreFichero(char * nombre, int indice, char * dst){
    const int base = 10;
    strcpy(dst, nombre);
    strcat(dst, itoa(indice, base));
}

void do_psplit(int l, int b, int s, int fd, char * name){
    char buffer [s+1];  // almacenará los datos leidos de fichero
    char nombre_fich [NAME_MAX+1]; // + 1 porque no incluye el char \0 en la especificacion de NAME_MAX.

    int offset, offset_W, indice;
    /*
     * 'offset_W' se utiliza para el error de write y para asegurarnos de que se escribe todo lo que deberia con write()
     * 'offsset' se utiliza para adelantar el buffer en caso de que ya se haya escrito una parte de los bytes leidos
    */
    offset = offset_W = indice = 0;

    // Primer fichero que se crea
    nombreFichero(name, indice, nombre_fich);
    int fd_i;
    if ((fd_i = open(nombre_fich , O_RDWR | O_CREAT | O_TRUNC, S_IRWXU)) == -1){
        perror("do_psplit (open)");
        exit(EXIT_FAILURE);
    }

    int b_escribir = b;	// bytes a escribir en cada iteracion de lectura, para la opcion -b
    int i, saltos;  // variables que se usaran para la opcion -l
    i = saltos = 0;
    int bytesLeidos = 0;    // bytes que leemos con read()

    while ((bytesLeidos = read(fd, buffer, s))) {
        if(b){
            offset = 0;	
            while (bytesLeidos > 0) {
                if (!b_escribir) {
                    if (fsync(fd_i)){
                        perror("do_psplit (fsync)");
                        exit(EXIT_FAILURE);
                    }
                    TRY ( close(fd_i) );
                    indice++;
                    nombreFichero(name, indice, nombre_fich);
                    if ((fd_i = open(nombre_fich , O_RDWR | O_CREAT | O_TRUNC, S_IRWXU)) == -1){
                        perror("do_psplit (open)");
                        exit(EXIT_FAILURE);
                    }
                    b_escribir = b;	// volvemos a establecer que hay que escribir un total de 'b' bytes
                }
                offset_W = 0;
                // El minimo se calcula para que no se intenten escribir mas caracteres de la cuenta.
                while( (offset_W += write(fd_i, buffer+offset+offset_W, MIN(bytesLeidos, b_escribir)-offset_W )) != MIN(bytesLeidos, b_escribir) ){
                    if(offset_W < 0)
                    {
                        perror("write");
                        exit(EXIT_FAILURE);
                    }
                }

                offset += offset_W;
                b_escribir -= offset_W;
                bytesLeidos -= offset_W;
            }
        }
        else{
            i = 0;
            offset = 0;
            while(i < bytesLeidos){
                if(saltos == l){
                    if(fsync(fd_i)){
                        perror("do_psplit (fsync)");
                        exit(EXIT_FAILURE);
                    }
                    TRY( close(fd_i) );
                    indice++;
                    nombreFichero(name, indice, nombre_fich);
                    if ((fd_i = open(nombre_fich , O_RDWR | O_CREAT | O_TRUNC, S_IRWXU)) == -1){
                        perror("do_psplit (open)");
                        exit(EXIT_FAILURE);
                    }
                    saltos = 0;
                }
                
                do{
                    if(buffer[i] == '\n')
                        saltos++; 
                    i++;
                }while((i < bytesLeidos) && (saltos < l));

                offset_W = 0;
                while( (offset_W += write(fd_i, buffer+offset+offset_W, i-offset-offset_W )) != i-offset ){
                    if(offset_W < 0)
                    {
                        perror("write");
                        exit(EXIT_FAILURE);
                    }
                }
                offset = i;
            }
        }
    }
    if (fsync(fd_i)){
        perror("do_psplit (fsync)");
        exit(EXIT_FAILURE);
    }
    TRY ( close(fd_i) );
}

void run_psplit(struct execcmd* ecmd)
{
    char errPsplit[] = {'s','p','l','b'};
    const int MAX_BUF_SIZE = pow(2, 20);
	int opt, l, b, s, p, error, flag_b, flag_l;
    l = b = error = flag_l = flag_b = 0;
    s = 1024;
    p = 1;
    optind = 1;
    while (!error && (opt = getopt(ecmd->argc, ecmd->argv, "l:b:s:p:h")) != -1) {
        switch (opt) {
            case 'l':
                if(flag_b) error = 1;
                else{
                    l = atoi(optarg);
                    if(l <= 0) error = 4;
                    else
                        flag_l = 1;
                }
                break;
            case 'b':
                if(flag_l) error = 1;
                else{
                    b = atoi(optarg);
                    if(b <= 0) error = 5;
                    else
                        flag_b = 1;
                }
                break;
            case 's':
                s = atoi(optarg);
                if(s <= 0 || s > MAX_BUF_SIZE) error = 2;
                break;
            case 'p':
                p = atoi(optarg);
                if(p <= 0) error = 3;
                break;
            case 'h':
                printf("%s\n", help_psplit());
                return;
                break;
            default:
                fprintf(stderr, "Uso: psplit [-l NLINES] [-b NBYTES] [-s BSIZE] [-p PROCS] [FILE1] [FILE2]...\n");
        }
    }
    switch(error){
        case 1:
            fprintf(stderr, "psplit: Opciones incompatibles\n");
            break;
        case 2:
        case 3:
        case 4:
        case 5:
            fprintf(stderr, "psplit: Opción -%c no válida\n", errPsplit[error-2]);
            break;
    }
    if(!error){

        if(optind == ecmd->argc){   // No mas argumentos que leer => lectura de la entrada estándar
            char * file_in = "stdin";
            do_psplit(l, b, s, STDIN_FILENO, file_in);
        }
        else {  // Procesamiento de los ficheros en paralelo según la opción -p
            pid_t procs_psplit[p];
            int cola, cabeza;
            cola = cabeza = 0;
            int BPROCS = p;
            int fd;

            block_sigchld();

            for(int i = optind; i < ecmd->argc; i++){
                if(BPROCS > 0) {    // Cuando aún tenemos opción de lanzar más en paralelo
                    if((procs_psplit[cabeza] = fork_or_panic("fork psplit")) == 0){
                        if ((fd = open(ecmd->argv[i], O_RDONLY, S_IRWXU)) == -1){
                            perror("run_psplit (open)");
                            exit(EXIT_FAILURE);
                        }
                        do_psplit(l, b, s, fd, ecmd->argv[i]);
                        TRY ( close(fd) );
                        exit(EXIT_SUCCESS);
                    }
                    BPROCS--;
                    cabeza = (cabeza + 1) % p;
                }
                else {  // Primero debemos esperar que finalice el más antiguo
                    if (waitpid(procs_psplit[cola], 0, 0) == -1){
                        perror("run_psplit (waitpid)");
                        exit(EXIT_FAILURE);
                    }
                    cola = (cola + 1) % p;
                    if((procs_psplit[cabeza] = fork_or_panic("fork psplit")) == 0){
                        if ((fd = open(ecmd->argv[i], O_RDONLY, S_IRWXU)) == -1){
                            perror("run_psplit (open)");
                            exit(EXIT_FAILURE);
                        }
                        do_psplit(l, b, s, fd, ecmd->argv[i]);
                        TRY ( close(fd) );
                        exit(EXIT_SUCCESS);
                    }
                    cabeza = (cabeza + 1) % p;
                }
            }

            // Esperamos en orden a que acaben todos los procesos en paralelo
            for(int i = 0; i < MIN(p, ecmd->argc - optind); i++){
                if (waitpid(procs_psplit[cola], 0, 0) == -1){
                    perror("run_psplit (waitpid)");
                    exit(EXIT_FAILURE);
                }
                cola = (cola + 1) % p;
            }
        }

        unblock_sigchld();

    }
}

// 'PIDS' almacenará el PID de los procesos que se estén ejecutando en segundo plano
pid_t PIDS[MAX_2PLANO] = {-1, -1, -1, -1, -1, -1, -1, -1};

// guardar_pid(pid) asume que siempre habrá espacio en PIDS para almacenar 'pid'
void guardar_pid(pid_t pid)
{   
    int i = 0;
    while(i < MAX_2PLANO && PIDS[i]!= -1)
        i++;
    PIDS[i] = pid;
}

// eliminar_pid(pid) asume que el 'pid' especificado como argumento esté dentro de PIDS
void eliminar_pid(pid_t pid)
{
    int i = 0;
    while(i < MAX_2PLANO && PIDS[i]!= pid)
        i++;
    PIDS[i] = -1;
}

// muestra todos los PIDS que tenemos almacenados en 'PIDS'
void listar_pids()
{
    for (int i = 0; i < MAX_2PLANO; ++i)
        if (PIDS[i] != -1)
            printf("[%d]\n", PIDS[i]);
}

// envia la señal SIGKILL a todos los procesos cuyo PID se encuentre almacenado en 'PIDS'
void matarTodos_pids()
{
    for (int i = 0; i < MAX_2PLANO; ++i)
        if (PIDS[i] != -1)
            if (kill(PIDS[i], SIGKILL) == -1){
                perror("kill");
                exit(EXIT_FAILURE);
            }
}

// Metodo para añadir '[]' directamente a un entero en base 10
char* itoa_con_corchetes(int val){
    static char buf[32] = {0};
    
    buf[30] = '\n';
    buf[29] = ']';
    int i = 28;
    for(; val && i ; --i, val /= 10)
        buf[i] = "0123456789"[val % 10];
    buf[i] = '[';
    
    return &buf[i];
}

// Manejador de señal SIGCHLD
void handle_sigchld(int sig) {
    int saved_errno = errno;
    pid_t pid = 0;
    int len, offset;

    while((pid = waitpid((pid_t)(-1), 0, WNOHANG)) > 0){
        char * proc = itoa_con_corchetes(pid);
        len = strlen(proc);

        offset = 0;
        while ((offset += write(STDOUT_FILENO, proc+offset, len)) != len){
            len -= offset;
            if(offset < 0)
            {
                perror("write");
                exit(EXIT_FAILURE);
            }
        }

        eliminar_pid(pid);
    }

    errno = saved_errno;
}

// Funciones para bloquear y desbloquear la señal SIGCHLD

void block_sigchld(){
    // Preparamos la máscara para bloquear la señal sigchld
    sigset_t blocked_signals_CHLD;
    if (sigemptyset(&blocked_signals_CHLD) == -1) {
        perror("sigemptyset");
        exit(EXIT_FAILURE);
    }
    if (sigaddset(&blocked_signals_CHLD, SIGCHLD)){
        perror("sigaddset");
        exit(EXIT_FAILURE);
    }
    
    // Bloqueamos la señale SIGCHLD
    if(sigprocmask(SIG_BLOCK, &blocked_signals_CHLD, NULL) == -1){
        perror("sigprocmask (block SIGCHLD)");
        exit(EXIT_FAILURE);
    }
}

void unblock_sigchld(){
    sigset_t blocked_signals_CHLD;
    if (sigemptyset(&blocked_signals_CHLD) == -1) {
        perror("sigemptyset");
        exit(EXIT_FAILURE);
    }
    if (sigaddset(&blocked_signals_CHLD, SIGCHLD)){
        perror("sigaddset");
        exit(EXIT_FAILURE);
    }
    
    // Desbloqueamos la señale SIGCHLD
    if(sigprocmask(SIG_UNBLOCK, &blocked_signals_CHLD, NULL) == -1){
        perror("sigprocmask (unblock SIGCHLD)");
        exit(EXIT_FAILURE);
    }
}


char * help_bjobs()
{
    return "Uso : bjobs [ - k ] [ - h ]\n\tOpciones :\n\t-k Mata todos los procesos en segundo plano.\n\t-h Ayuda\n";
}

void run_bjobs(struct execcmd* ecmd)
{
    int opt, error, flag_k;
    opt = error = flag_k = 0;

    optind = 1;
    while (!error && (opt = getopt(ecmd->argc, ecmd->argv, "kh")) != -1) {
        switch (opt) {
            case 'k':
                flag_k = 1;
                break;
            case 'h':
                printf("%s\n", help_bjobs());
                return;
                break;
            default:
                error = 1;
        }
    }

    if (!error){
        (flag_k) ? matarTodos_pids() : listar_pids();
    }
}

// Devuelve el indice del comando interno que tiene asignado o -1 en caso de no serlo
int cmd_esInterno(char* cmd)
{
    int i = 0;
    while(i < N_INTERNOS && strcmp(cmd, comandosInternos[i]) != 0) {
        i++;
    }

    if (i < N_INTERNOS)
        return i;
    else
        return -1;
}

// En funcion del numeroComando proporcionado, ejecuta el metodo correspondiente
void ejecutar_interno(struct execcmd* ecmd, int numeroComando) {
    switch (numeroComando) {
        case 0:
            run_cwd();
            break;

        case 1:
            run_exit();
            break;

        case 2:
            run_cd(ecmd);
            break;

        case 3:
        	run_psplit(ecmd);
        	break;
        case 4:
            run_bjobs(ecmd);
            break;
    }
}


/******************************************************************************
 * Funciones para realizar el análisis sintáctico de la línea de órdenes
 ******************************************************************************/


// `get_token` recibe un puntero al principio de una cadena (`start_of_str`),
// otro puntero al final de esa cadena (`end_of_str`) y, opcionalmente, dos
// punteros para guardar el principio y el final del token, respectivamente.
//
// `get_token` devuelve un *token* de la cadena de entrada.

int get_token(char** start_of_str, char const* end_of_str,
        char** start_of_token, char** end_of_token)
{
    char* s;
    int ret;

    // Salta los espacios en blanco
    s = *start_of_str;
    while (s < end_of_str && strchr(WHITESPACE, *s))
        s++;

    // `start_of_token` apunta al principio del argumento (si no es NULL)
    if (start_of_token)
        *start_of_token = s;

    ret = *s;
    switch (*s)
    {
        case 0:
            break;
        case '|':
        case '(':
        case ')':
        case ';':
        case '&':
        case '<':
            s++;
            break;
        case '>':
            s++;
            if (*s == '>')
            {
                ret = '+';
                s++;
            }
            break;

        default:

            // El caso por defecto (cuando no hay caracteres especiales) es el
            // de un argumento de un comando. `get_token` devuelve el valor
            // `'a'`, `start_of_token` apunta al argumento (si no es `NULL`),
            // `end_of_token` apunta al final del argumento (si no es `NULL`) y
            // `start_of_str` avanza hasta que salta todos los espacios
            // *después* del argumento. Por ejemplo:
            //
            //     |-----------+---+---+---+---+---+---+---+---+---+-----------|
            //     | (espacio) | a | r | g | u | m | e | n | t | o | (espacio)
            //     |
            //     |-----------+---+---+---+---+---+---+---+---+---+-----------|
            //                   ^                                   ^
            //            start_o|f_token                       end_o|f_token

            ret = 'a';
            while (s < end_of_str &&
                    !strchr(WHITESPACE, *s) &&
                    !strchr(SYMBOLS, *s))
                s++;
            break;
    }

    // `end_of_token` apunta al final del argumento (si no es `NULL`)
    if (end_of_token)
        *end_of_token = s;

    // Salta los espacios en blanco
    while (s < end_of_str && strchr(WHITESPACE, *s))
        s++;

    // Actualiza `start_of_str`
    *start_of_str = s;

    return ret;
}


// `peek` recibe un puntero al principio de una cadena (`start_of_str`), otro
// puntero al final de esa cadena (`end_of_str`) y un conjunto de caracteres
// (`delimiter`).
//
// El primer puntero pasado como parámero (`start_of_str`) avanza hasta el
// primer carácter que no está en el conjunto de caracteres `WHITESPACE`.
//
// `peek` devuelve un valor distinto de `NULL` si encuentra alguno de los
// caracteres en `delimiter` justo después de los caracteres en `WHITESPACE`.

int peek(char** start_of_str, char const* end_of_str, char* delimiter)
{
    char* s;

    s = *start_of_str;
    while (s < end_of_str && strchr(WHITESPACE, *s))
        s++;
    *start_of_str = s;

    return *s && strchr(delimiter, *s);
}


// Definiciones adelantadas de funciones
struct cmd* parse_line(char**, char*);
struct cmd* parse_pipe(char**, char*);
struct cmd* parse_exec(char**, char*);
struct cmd* parse_subs(char**, char*);
struct cmd* parse_redr(struct cmd*, char**, char*);
struct cmd* null_terminate(struct cmd*);


// `parse_cmd` realiza el *análisis sintáctico* de la línea de órdenes
// introducida por el usuario.
//
// `parse_cmd` utiliza `parse_line` para obtener una estructura `cmd`.

struct cmd* parse_cmd(char* start_of_str)
{
    char* end_of_str;
    struct cmd* cmd;

    DPRINTF(DBG_TRACE, "STR\n");

    end_of_str = start_of_str + strlen(start_of_str);

    cmd = parse_line(&start_of_str, end_of_str);

    // Comprueba que se ha alcanzado el final de la línea de órdenes
    peek(&start_of_str, end_of_str, "");
    if (start_of_str != end_of_str)
        error("%s: error sintáctico: %s\n", __func__);

    DPRINTF(DBG_TRACE, "END\n");

    return cmd;
}


// `parse_line` realiza el análisis sintáctico de la línea de órdenes
// introducida por el usuario.
//
// `parse_line` comprueba en primer lugar si la línea contiene alguna tubería.
// Para ello `parse_line` llama a `parse_pipe` que a su vez verifica si hay
// bloques de órdenes y/o redirecciones.  A continuación, `parse_line`
// comprueba si la ejecución de la línea se realiza en segundo plano (con `&`)
// o si la línea de órdenes contiene una lista de órdenes (con `;`).

struct cmd* parse_line(char** start_of_str, char* end_of_str)
{
    struct cmd* cmd;
    int delimiter;

    cmd = parse_pipe(start_of_str, end_of_str);

    while (peek(start_of_str, end_of_str, "&"))
    {
        // Consume el delimitador de tarea en segundo plano
        delimiter = get_token(start_of_str, end_of_str, 0, 0);
        assert(delimiter == '&');

        // Construye el `cmd` para la tarea en segundo plano
        cmd = backcmd(cmd);
    }

    if (peek(start_of_str, end_of_str, ";"))
    {
        if (cmd->type == EXEC && ((struct execcmd*) cmd)->argv[0] == 0)
            error("%s: error sintáctico: no se encontró comando\n", __func__);

        // Consume el delimitador de lista de órdenes
        delimiter = get_token(start_of_str, end_of_str, 0, 0);
        assert(delimiter == ';');

        // Construye el `cmd` para la lista
        cmd = listcmd(cmd, parse_line(start_of_str, end_of_str));
    }

    return cmd;
}


// `parse_pipe` realiza el análisis sintáctico de una tubería de manera
// recursiva si encuentra el delimitador de tuberías '|'.
//
// `parse_pipe` llama a `parse_exec` y `parse_pipe` de manera recursiva para
// realizar el análisis sintáctico de todos los componentes de la tubería.

struct cmd* parse_pipe(char** start_of_str, char* end_of_str)
{
    struct cmd* cmd;
    int delimiter;

    cmd = parse_exec(start_of_str, end_of_str);

    if (peek(start_of_str, end_of_str, "|"))
    {
        if (cmd->type == EXEC && ((struct execcmd*) cmd)->argv[0] == 0)
            error("%s: error sintáctico: no se encontró comando\n", __func__);

        // Consume el delimitador de tubería
        delimiter = get_token(start_of_str, end_of_str, 0, 0);
        assert(delimiter == '|');

        // Construye el `cmd` para la tubería
        cmd = pipecmd(cmd, parse_pipe(start_of_str, end_of_str));
    }

    return cmd;
}


// `parse_exec` realiza el análisis sintáctico de un comando a no ser que la
// expresión comience por un paréntesis, en cuyo caso se llama a `parse_subs`.
//
// `parse_exec` reconoce las redirecciones antes y después del comando.

struct cmd* parse_exec(char** start_of_str, char* end_of_str)
{
    char* start_of_token;
    char* end_of_token;
    int token, argc;
    struct execcmd* cmd;
    struct cmd* ret;

    // ¿Inicio de un bloque?
    if (peek(start_of_str, end_of_str, "("))
        return parse_subs(start_of_str, end_of_str);

    // Si no, lo primero que hay en una línea de órdenes es un comando

    // Construye el `cmd` para el comando
    ret = execcmd();
    cmd = (struct execcmd*) ret;

    // ¿Redirecciones antes del comando?
    ret = parse_redr(ret, start_of_str, end_of_str);

    // Bucle para separar los argumentos de las posibles redirecciones
    argc = 0;
    while (!peek(start_of_str, end_of_str, "|)&;"))
    {
        if ((token = get_token(start_of_str, end_of_str,
                        &start_of_token, &end_of_token)) == 0)
            break;

        // El siguiente token debe ser un argumento porque el bucle
        // para en los delimitadores
        if (token != 'a')
            error("%s: error sintáctico: se esperaba un argumento\n", __func__);

        // Almacena el siguiente argumento reconocido. El primero es
        // el comando
        cmd->argv[argc] = start_of_token;
        cmd->eargv[argc] = end_of_token;
        cmd->argc = ++argc;
        if (argc >= MAX_ARGS)
            panic("%s: demasiados argumentos\n", __func__);

        // ¿Redirecciones después del comando?
        ret = parse_redr(ret, start_of_str, end_of_str);
    }

    // El comando no tiene más parámetros
    cmd->argv[argc] = 0;
    cmd->eargv[argc] = 0;

    return ret;
}


// `parse_subs` realiza el análisis sintáctico de un bloque de órdenes
// delimitadas por paréntesis o `subshell` llamando a `parse_line`.
//
// `parse_subs` reconoce las redirecciones después del bloque de órdenes.

struct cmd* parse_subs(char** start_of_str, char* end_of_str)
{
    int delimiter;
    struct cmd* cmd;
    struct cmd* scmd;

    // Consume el paréntesis de apertura
    if (!peek(start_of_str, end_of_str, "("))
        error("%s: error sintáctico: se esperaba '('", __func__);
    delimiter = get_token(start_of_str, end_of_str, 0, 0);
    assert(delimiter == '(');

    // Realiza el análisis sintáctico hasta el paréntesis de cierre
    scmd = parse_line(start_of_str, end_of_str);

    // Construye el `cmd` para el bloque de órdenes
    cmd = subscmd(scmd);

    // Consume el paréntesis de cierre
    if (!peek(start_of_str, end_of_str, ")"))
        error("%s: error sintáctico: se esperaba ')'", __func__);
    delimiter = get_token(start_of_str, end_of_str, 0, 0);
    assert(delimiter == ')');

    // ¿Redirecciones después del bloque de órdenes?
    cmd = parse_redr(cmd, start_of_str, end_of_str);

    return cmd;
}


// `parse_redr` realiza el análisis sintáctico de órdenes con
// redirecciones si encuentra alguno de los delimitadores de
// redirección ('<' o '>').

struct cmd* parse_redr(struct cmd* cmd, char** start_of_str, char* end_of_str)
{
    int delimiter;
    char* start_of_token;
    char* end_of_token;

    // Si lo siguiente que hay a continuación es delimitador de
    // redirección...
    while (peek(start_of_str, end_of_str, "<>"))
    {
        // Consume el delimitador de redirección
        delimiter = get_token(start_of_str, end_of_str, 0, 0);
        assert(delimiter == '<' || delimiter == '>' || delimiter == '+');

        // El siguiente token tiene que ser el nombre del fichero de la
        // redirección entre `start_of_token` y `end_of_token`.
        if ('a' != get_token(start_of_str, end_of_str, &start_of_token, &end_of_token))
            error("%s: error sintáctico: se esperaba un fichero", __func__);

        // Construye el `cmd` para la redirección
        switch(delimiter)
        {
            case '<':
                cmd = redrcmd(cmd, start_of_token, end_of_token, O_RDONLY, S_IRWXU, STDIN_FILENO);
                break;
            case '>':
                cmd = redrcmd(cmd, start_of_token, end_of_token, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU, STDOUT_FILENO);
                break;
            case '+': // >>
                cmd = redrcmd(cmd, start_of_token, end_of_token, O_WRONLY|O_CREAT|O_APPEND, S_IRWXU, STDOUT_FILENO);
                break;
        }
    }

    return cmd;
}


// Termina en NULL todas las cadenas de las estructuras `cmd`
struct cmd* null_terminate(struct cmd* cmd)
{
    struct execcmd* ecmd;
    struct redrcmd* rcmd;
    struct pipecmd* pcmd;
    struct listcmd* lcmd;
    struct backcmd* bcmd;
    struct subscmd* scmd;
    int i;

    if(cmd == 0)
        return 0;

    switch(cmd->type)
    {
        case EXEC:
            ecmd = (struct execcmd*) cmd;
            for(i = 0; ecmd->argv[i]; i++)
                *ecmd->eargv[i] = 0;
            break;

        case REDR:
            rcmd = (struct redrcmd*) cmd;
            null_terminate(rcmd->cmd);
            *rcmd->efile = 0;
            break;

        case PIPE:
            pcmd = (struct pipecmd*) cmd;
            null_terminate(pcmd->left);
            null_terminate(pcmd->right);
            break;

        case LIST:
            lcmd = (struct listcmd*) cmd;
            null_terminate(lcmd->left);
            null_terminate(lcmd->right);
            break;

        case BACK:
            bcmd = (struct backcmd*) cmd;
            null_terminate(bcmd->cmd);
            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;
            null_terminate(scmd->cmd);
            break;

        case INV:
        default:
            panic("%s: estructura `cmd` desconocida\n", __func__);
    }

    return cmd;
}


/******************************************************************************
 * Funciones para la ejecución de la línea de órdenes
 ******************************************************************************/


void exec_cmd(struct execcmd* ecmd)
{
    assert(ecmd->type == EXEC);

    if (ecmd->argv[0] == NULL) exit(EXIT_SUCCESS);

    execvp(ecmd->argv[0], ecmd->argv);

    panic("no se encontró el comando '%s'\n", ecmd->argv[0]);
}

void run_cmd(struct cmd* cmd)
{
    struct execcmd* ecmd;
    struct redrcmd* rcmd;
    struct listcmd* lcmd;
    struct pipecmd* pcmd;
    struct backcmd* bcmd;
    struct subscmd* scmd;
    int p[2];
    int fd;

    int comando;    // almacenará el número de comando interno o -1
    pid_t pid;      // 'pid' almacena el PID del proceso hijo al que se espera tras hacer un fork

    DPRINTF(DBG_TRACE, "STR\n");

    if(cmd == 0) return;

    switch(cmd->type)
    {
        case EXEC:
            ecmd = (struct execcmd*) cmd;
            if (ecmd->argv[0] != NULL) {
	            comando = cmd_esInterno(ecmd->argv[0]);
	            if (comando != -1)
	                ejecutar_interno(ecmd, comando);
	            else {
                    block_sigchld();
	                if ((pid = fork_or_panic("fork EXEC")) == 0)
	                    exec_cmd(ecmd);
	                TRY( waitpid(pid, 0, 0) );
                    unblock_sigchld();
	            }
	        }
            break;

        case REDR:

            rcmd = (struct redrcmd*) cmd;

            int fd_anterior;
            if ((fd_anterior = dup(rcmd->fd)) == -1){   // Guardamos el anterior descriptor de fichero
                perror("dup");
                exit(EXIT_FAILURE);
            }	
            TRY( close(rcmd->fd) );
            if ((fd = open(rcmd->file, rcmd->flags, rcmd->mode)) < 0)
            {
                    perror("open");
                    exit(EXIT_FAILURE);
            }

            if (rcmd->cmd->type == EXEC && (comando = cmd_esInterno((ecmd = (struct execcmd*) rcmd->cmd)->argv[0])) != -1)
            {
	            ejecutar_interno(ecmd, comando);
                TRY ( close(fd) );
                if ((fd = dup(fd_anterior)) == -1){
                    perror("dup");
                    exit(EXIT_FAILURE);
                }
            }
            else 
            {
                block_sigchld();
                if ((pid = fork_or_panic("fork REDR")) == 0)
                {
                    if (rcmd->cmd->type == EXEC)
                        exec_cmd(ecmd);
                    else
                        run_cmd(rcmd->cmd);

                    exit(EXIT_SUCCESS);
                }

                TRY( waitpid(pid, 0, 0) );
                TRY ( close(fd) );
                if ((fd = dup(fd_anterior)) == -1){
                    perror("dup");
                    exit(EXIT_FAILURE);
                }
                unblock_sigchld();
            }
 
   			TRY ( close(fd_anterior) );
            break;

        case LIST:
            lcmd = (struct listcmd*) cmd;

            run_cmd(lcmd->left);
            run_cmd(lcmd->right);
            break;

        case PIPE:
            pcmd = (struct pipecmd*)cmd;
            if (pipe(p) < 0)
            {
                perror("pipe");
                exit(EXIT_FAILURE);
            }

            // Ejecución del hijo de la izquierda
            pid_t pid_izq;
            block_sigchld();
            if ((pid_izq = fork_or_panic("fork PIPE left")) == 0)
            {
                TRY( close(STDOUT_FILENO) );
                TRY( dup(p[1]) );
                TRY( close(p[0]) );
                TRY( close(p[1]) );
                if (pcmd->left->type == EXEC){
                    ecmd = (struct execcmd*) pcmd->left;

                    comando = cmd_esInterno(ecmd->argv[0]);
                    if (comando != -1)
                        ejecutar_interno(ecmd, comando);
                    else
                        exec_cmd(ecmd);
                }
                else
                    run_cmd(pcmd->left);
                exit(EXIT_SUCCESS);
            }

            // Ejecución del hijo de la derecha
            pid_t pid_der;
            if ((pid_der = fork_or_panic("fork PIPE right")) == 0)
            {
                TRY( close(STDIN_FILENO) );
                TRY( dup(p[0]) );
                TRY( close(p[0]) );
                TRY( close(p[1]) );
                if (pcmd->right->type == EXEC){
                    ecmd = (struct execcmd*) pcmd->right;

                    comando = cmd_esInterno(ecmd->argv[0]);
                    if (comando != -1)
                        ejecutar_interno(ecmd, comando);
                    else
                        exec_cmd(ecmd);
                }
                else
                    run_cmd(pcmd->right);
                exit(EXIT_SUCCESS);
            }
            TRY( close(p[0]) );
            TRY( close(p[1]) );

            TRY( waitpid(pid_izq, 0, 0) );
            TRY( waitpid(pid_der, 0, 0) );
            unblock_sigchld();
            break;

        case BACK:
            bcmd = (struct backcmd*)cmd;
            if ((pid = fork_or_panic("fork BACK")) == 0)
            {
                if (bcmd->cmd->type == EXEC){
                    ecmd = (struct execcmd*) bcmd->cmd;

                    comando = cmd_esInterno(ecmd->argv[0]);
                    if (comando != -1)
                        ejecutar_interno(ecmd, comando);
                    else
                        exec_cmd(ecmd);
                }
                else
                    run_cmd(bcmd->cmd);

                exit(EXIT_SUCCESS);
            }
            else
            {
                printf("[%d]\n", pid);
                guardar_pid(pid);
            }
            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;
            block_sigchld();
            if ((pid = fork_or_panic("fork SUBS")) == 0)
            {
                run_cmd(scmd->cmd);
                exit(EXIT_SUCCESS);
            }
            TRY( waitpid(pid, 0, 0) );
            unblock_sigchld();
            break;

        case INV:
        default:
            panic("%s: estructura `cmd` desconocida\n", __func__);
    }

    DPRINTF(DBG_TRACE, "END\n");
}


void print_cmd(struct cmd* cmd)
{
    struct execcmd* ecmd;
    struct redrcmd* rcmd;
    struct listcmd* lcmd;
    struct pipecmd* pcmd;
    struct backcmd* bcmd;
    struct subscmd* scmd;

    if(cmd == 0) return;

    switch(cmd->type)
    {
        case EXEC:
            ecmd = (struct execcmd*) cmd;
            if (ecmd->argv[0] != 0)
                printf("fork( exec( %s ) )", ecmd->argv[0]);
            break;

        case REDR:
            rcmd = (struct redrcmd*) cmd;
            printf("fork( ");
            if (rcmd->cmd->type == EXEC)
                printf("exec ( %s )", ((struct execcmd*) rcmd->cmd)->argv[0]);
            else
                print_cmd(rcmd->cmd);
            printf(" )");
            break;

        case LIST:
            lcmd = (struct listcmd*) cmd;
            print_cmd(lcmd->left);
            printf(" ; ");
            print_cmd(lcmd->right);
            break;

        case PIPE:
            pcmd = (struct pipecmd*) cmd;
            printf("fork( ");
            if (pcmd->left->type == EXEC)
                printf("exec ( %s )", ((struct execcmd*) pcmd->left)->argv[0]);
            else
                print_cmd(pcmd->left);
            printf(" ) => fork( ");
            if (pcmd->right->type == EXEC)
                printf("exec ( %s )", ((struct execcmd*) pcmd->right)->argv[0]);
            else
                print_cmd(pcmd->right);
            printf(" )");
            break;

        case BACK:
            bcmd = (struct backcmd*) cmd;
            printf("fork( ");
            if (bcmd->cmd->type == EXEC)
                printf("exec ( %s )", ((struct execcmd*) bcmd->cmd)->argv[0]);
            else
                print_cmd(bcmd->cmd);
            printf(" )");
            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;
            printf("fork( ");
            print_cmd(scmd->cmd);
            printf(" )");
            break;

        case INV:
        default:
            panic("%s: estructura `cmd` desconocida\n", __func__);
    }
}


void free_cmd(struct cmd* cmd)
{
    struct execcmd* ecmd;
    struct redrcmd* rcmd;
    struct listcmd* lcmd;
    struct pipecmd* pcmd;
    struct backcmd* bcmd;
    struct subscmd* scmd;

    if(cmd == 0) return;

    switch(cmd->type)
    {
        case EXEC:
            break;

        case REDR:
            rcmd = (struct redrcmd*) cmd;
            free_cmd(rcmd->cmd);

            free(rcmd->cmd);
            break;

        case LIST:
            lcmd = (struct listcmd*) cmd;

            free_cmd(lcmd->left);
            free_cmd(lcmd->right);

            free(lcmd->right);
            free(lcmd->left);
            break;

        case PIPE:
            pcmd = (struct pipecmd*) cmd;

            free_cmd(pcmd->left);
            free_cmd(pcmd->right);

            free(pcmd->right);
            free(pcmd->left);
            break;

        case BACK:
            bcmd = (struct backcmd*) cmd;

            free_cmd(bcmd->cmd);

            free(bcmd->cmd);
            break;

        case SUBS:
            scmd = (struct subscmd*) cmd;

            free_cmd(scmd->cmd);

            free(scmd->cmd);
            break;

        case INV:
        default:
            panic("%s: estructura `cmd` desconocida\n", __func__);
    }
}


/******************************************************************************
 * Lectura de la línea de órdenes con la biblioteca libreadline
 ******************************************************************************/


// `get_cmd` muestra un *prompt* y lee lo que el usuario escribe usando la
// biblioteca readline. Ésta permite mantener el historial, utilizar las flechas
// para acceder a las órdenes previas del historial, búsquedas de órdenes, etc.

char* get_cmd()
{
    char* buf;

    uid_t uid = getuid();

    struct passwd* passwd = getpwuid(uid);
    if (!passwd) {
        perror("getpwuid");
        exit(EXIT_FAILURE);
    }
    char* user = passwd->pw_name;
    char path[PATH_MAX];
    if(!getcwd(path, PATH_MAX)){
        perror("getcwd");
        exit(EXIT_FAILURE);
    }

    char* dir = basename(path);

    // Calculamos cuantos espacios extra necesitamos para el 'prompt'
    char caracteres_extra[] = {'@', '>', ' ', '\0'};

    // Usamos 'sizeof' para que tambien se contabilice el '\0'
    char prompt[strlen(user) + strlen(dir) + sizeof(caracteres_extra)];

    sprintf(prompt, "%s@%s> ", user, dir);

    // Lee la orden tecleada por el usuario
    buf = readline(prompt);

    // Si el usuario ha escrito una orden, almacenarla en la historia.
    if(buf)
        add_history(buf);

    return buf;
}


/******************************************************************************
 * Bucle principal de `simplesh`
 ******************************************************************************/


void help(char **argv)
{
    info("Usage: %s [-d N] [-h]\n\
         shell simplesh v%s\n\
         Options: \n\
         -d set debug level to N\n\
         -h help\n\n",
         argv[0], VERSION);
}


void parse_args(int argc, char** argv)
{
    int option;

    // Bucle de procesamiento de parámetros
    while((option = getopt(argc, argv, "d:h")) != -1) {
        switch(option) {
            case 'd':
                g_dbg_level = atoi(optarg);
                break;
            case 'h':
            default:
                help(argv);
                exit(EXIT_SUCCESS);
                break;
        }
    }
}

int main(int argc, char** argv)
{
    // Bloqueamos la señal SIGINT
    sigset_t blocked_signals;
    if (sigemptyset(&blocked_signals)){
        perror("sigemptyset");
        exit(EXIT_FAILURE);
    }
    if (sigaddset(&blocked_signals, SIGINT)){
        perror("sigaddset");
        exit(EXIT_FAILURE);
    }

    if(sigprocmask(SIG_BLOCK, &blocked_signals, NULL) == -1){
        perror("sigprocmask (SIGINT)");
        exit(EXIT_FAILURE);
    }

    // Ignoramos la señal SIGQUIT
    struct sigaction ign_sigquit;
    memset(&ign_sigquit, 0, sizeof(struct sigaction));
    ign_sigquit.sa_handler = SIG_IGN;
    if (sigemptyset(&ign_sigquit.sa_mask)){
        perror("sigemptyset");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGQUIT, &ign_sigquit, NULL) == -1) {
        perror("sigaction (SIGQUIT)");
        exit(EXIT_FAILURE);
    }

    // Cosecha de procesos zombies con manejador de SIGCHLD
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = &handle_sigchld;
    if(sigemptyset(&sa.sa_mask)){
        perror("sigemptyset");
        exit(EXIT_FAILURE);
    }
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror("sigaction (SIGCHLD)");
        exit(EXIT_FAILURE);
    }

    char* buf;

    parse_args(argc, argv);

    DPRINTF(DBG_TRACE, "STR\n");

    // Borramos la variable de entorno OLDPWD.
    if (unsetenv("OLDPWD") == -1){
        perror("unsetenv");
        exit(EXIT_FAILURE);
    }

    // Bucle de lectura y ejecución de órdenes
    while ((buf = get_cmd()) != NULL)
    {
        // Realiza el análisis sintáctico de la línea de órdenes
        cmd = parse_cmd(buf);

        // Termina en `NULL` todas las cadenas de las estructuras `cmd`
        null_terminate(cmd);

        DBLOCK(DBG_CMD, {
            info("%s:%d:%s: print_cmd: ",
                 __FILE__, __LINE__, __func__);
            print_cmd(cmd); printf("\n"); fflush(NULL); } );

        // Ejecuta la línea de órdenes
        run_cmd(cmd);

        // Libera la memoria de las estructuras 'cmd'
        free_cmd(cmd);

        // Libera al propio 'cmd' (BOLETIN 1: EJERCICIO 3)
        free(cmd);

        // Libera la memoria de la línea de órdenes
        free(buf);
    }

    DPRINTF(DBG_TRACE, "END\n");

    return 0;
}
