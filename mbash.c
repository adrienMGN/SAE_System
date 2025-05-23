#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>

/* ###############
 *    VARIABLES
 * ############### */

#define MAXLI 2048
#define MAX_VARIABLES 100 // Nombre maximum de variables
#define MAX_VAR_LENGTH 256 // Taille maximale pour une variable (nom=valeur)
// Simule un tableau de variables d'environnement (VAR=valeur)
char *tab_variables[100];
int size_tab_variables = 0; // Nombre de variables enregistrÃ©es

char cmd[MAXLI];
char* args[MAXLI]; // Tableau qui contient Ã  l'index 0 la commande et aux autres index les arguments

int arg_count; // ???

char* home; // On stock le path du home qui ne change pas
char* mbashrc_path; // Path du mbashrc stockÃ© dans cette var car il ne change pas



// Structure pour associer des commandes et leurs descriptions
typedef struct {
    const char *command;
    const char *description;
} HelpEntry;

// Liste des commandes et descriptions
HelpEntry help_entries[] = {
    {"cd", "Change le rÃ©pertoire courant. Utilisation : cd [rÃ©pertoire]"},
    {"pwd", "Affiche le rÃ©pertoire courant."},
    {"echo", "Affiche des arguments ou du texte. Utilisation : echo [texte]"},
    {"exit", "Quitte le shell. Utilisation : exit [code_de_sortie]"},
    {"env", "Affiche les variables d'environnement."},
    {"unset", "Supprime une variable d'environnement. Utilisation : unset [variable]"},
    {"export", "Ajoute ou modifie une variable d'environnement. Utilisation : export [variable=valeur]"},
    {"help", "Affiche cette liste d'aide ou des dÃ©tails sur une commande. Utilisation : help [commande]"},
    {NULL, NULL} // Marqueur de fin
};












/* #############
 *     ETATS
 * ############# */

#define S_DEBUT 0
#define S_COMMANDE 1
#define S_PIPE 2 // |
#define S_SEP 3 // ;
#define S_AND 4 // &&
#define S_BG 5 // &
#define S_FINI 6
#define S_ERREUR 7






/* ################################
 *     PROTOTYPES DES FONCTIONS
 * ################################ */

void mbash(char *cmd, char **envp);
void process_with_automaton(char *input, char **envp);
void handle_pipe(char *cmd1, char *cmd2, char **envp);
void changeDirectory(char *path);
void execute(char **envp);
char** parseArguments(char *cmd);
int handleBuiltInCmd();
void lookForVariables();
char* extract_between_quotes(const char* source);
void exportVariable(char* var);
char* lookForAliases(char* alias);
char* getINFO();
void help(char* cmdName);









/**
 * MÃ©thode qui traite la commande donnÃ©e par l'utilisateur via un automate
 * pour gÃ©rer les pipes, executions en fond, etc...
 */
void process_with_automaton(char* input, char** envp) {

    int state = S_DEBUT;
    int i = 0;
    char current_command[MAXLI] = {0};
    char* cmd1 = NULL;
    char* cmd2 = NULL;

    // DÃ©but de l'automate
    while (state != S_FINI) {
        char c = input[i++];

        switch (state) {

            case S_DEBUT:
                if (c == '\0') {
                    state = S_FINI; // fin de chaine donc fin
                } else if (c != ' ') {
                    strncat(current_command, &c, 1);
                    state = S_COMMANDE;
                }
                break;

            case S_COMMANDE:
                if (c == '|') {
                    cmd1 = strdup(current_command);
                    memset(current_command, 0, sizeof(current_command));
                    state = S_PIPE;
                } else if (c == ';') {
                    mbash(current_command, envp);
                    memset(current_command, 0, sizeof(current_command));
                    state = S_SEP;
                } else if (c == '&') {
                    if (input[i] == '&') {
                        i++;
                        cmd1 = strdup(current_command);
                        memset(current_command, 0, sizeof(current_command));
                        state = S_AND;
                    } else {
                        cmd1 = strdup(current_command);
                        memset(current_command, 0, sizeof(current_command));
                        state = S_BG;
                    }
                } else if (c == '\0') {
                    mbash(current_command, envp);
                    state = S_FINI;
                } else {
                    strncat(current_command, &c, 1);
                }
                break;

            case S_PIPE:
                if (c != '\0') {
                    strncat(current_command, &c, 1);
                } else {
                    cmd2 = strdup(current_command);
                    handle_pipe(cmd1, cmd2, envp);
                    state = S_FINI;
                }
                break;

            case S_SEP:
                if (c != '\0') {
                    strncat(current_command, &c, 1);
                    state = S_COMMANDE;
                }
                break;

            case S_AND:
                if (c != '\0') {
                    strncat(current_command, &c, 1);
                } else {
                    if (mbash(cmd1, envp), 1) {
                        mbash(current_command, envp);
                    }
                    state = S_FINI;
                }
                break;

            case S_BG:
                if (fork() == 0) {
                    mbash(cmd1, envp);
                    exit(0);
                }
                state = S_DEBUT;
                break;

            case S_ERREUR:
                fprintf(stderr, "Erreur de syntaxe\n");
                return;
        }
    }

}






/**
 * MÃ©thode ...
 */
void handle_pipe(char* cmd1, char* cmd2, char** envp) {
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        return;
    }
    if (fork() == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);
        mbash(cmd1, envp);
        exit(0);
    }
    if (fork() == 0) {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        mbash(cmd2, envp);
        exit(0);
    }
    close(fd[0]);
    close(fd[1]);
    wait(NULL);
    wait(NULL);
}






/**
 * MÃ©thode qui extrait une chaÃ®ne de caractÃ¨res entre quotes
 * @return La chaÃ®ne de caractÃ¨res qui se situait entre quotes
 */
char* extract_between_quotes(const char* source) {

    const char* start = strchr(source, '"');
    if (start) {
        start++;
        const char* end = strchr(start, '"');
        if (end) {
            size_t length = end - start;
            char* result = malloc(length + 1);
            if (!result) {
                perror("malloc");
                return NULL;
            }
            strncpy(result, start, length);
            result[length] = '\0';
            return result;
        }
    }
    return NULL;

}






/**
 * MÃ©thode qui donne les informations sur le systeme (nom de la machine, et le PWD)
 * @return Les informations du current working directory de l'utilisateur
 */
char * getINFO() {

  // Informaton sur l'utilisateur : rÃ©cupÃ©rer avec la mÃ©thode gethostname
  char hostname[MAXLI];
  gethostname(hostname, MAXLI);

  // On alloue la quantitÃ© nÃ©cessaire Ã  l'affichage
  char * info = malloc(MAXLI + 10);

  // On ajout le nom et le chemin du repertoire courant dans la chaine de caractere
  sprintf(info, "%s:%s$ ", hostname, getcwd(NULL, 0));

  // On retourne la chaÃ®ne
  return info;

}






/**
 * MÃ©thode qui cherche si la commande est un alias prÃ©sent dans le fichier .mbashrc
 * @return Renvoie la chaÃ®ne donnÃ©e en paramÃ¨tre si l'alias n'exsite pas, sinon renvoie la commande liÃ©e Ã  l'alias
 */
char* lookForAliases(char* alias) {

  // On ouvre le fichier qui contient les alias
  FILE* configFile = fopen(mbashrc_path, "r");
  if (!configFile) {
      perror("fopen");
      return alias;
  }

  char currentLine[1000];

  // On parcours le fichier ligne par ligne
  while (fgets(currentLine, sizeof(currentLine), configFile)) {

    // Suppression du caractÃ¨re de nouvelle ligne
    currentLine[strcspn(currentLine, "\n")] = 0;

    // RÃ©cupÃ©ration de la partie avant et aprÃ¨s le signe "="
    char* name = strtok(currentLine, "=");
    char* value = strtok(NULL, "=");
    alias[strcspn(alias, "\n")] = 0;

    // Si l'alias correspond
    if (strcmp(name, alias) == 0) {
        // On extrait la commande entre guillemets
        char* newCmd = extract_between_quotes(value);

        // On ferme le fichier et on renvoie la commande
        fclose(configFile);
        return newCmd;
    }

  }

  // Si l'alias n'est pas trouvÃ©e, on ferme le fichier et on renvoie une chaÃ®ne vide
  fclose(configFile);
  return alias; // Alias non trouvÃ©, on renvoie l'alias de base

}






/*
 * MÃ©thode qui change le rÃ©pertoire courant
 */
void changeDirectory(char *path) {

  // Si le chemin est vide, on va dans le rÃ©pertoire personnel de l'utilisateur
  if (path == NULL) {
    path = getenv("HOME");
  }
  // Changement de rÃ©pertoire
  if (chdir(path) != 0) {
    perror("chdir");
  }

}






/**
 *
 */
void help(char* cmdName) {

  if (cmdName == NULL) {
    // Afficher toutes les commandes
    printf("Liste des commandes disponibles :\n");
    for (int i = 0; help_entries[i].command != NULL; i++) {
        printf("  %s : %s\n", help_entries[i].command, help_entries[i].description);
    }
  }
  else {
    // Recherche de la commande dans la liste
    for (int i = 0; help_entries[i].command != NULL; i++) {
      if (strcmp(help_entries[i].command, cmdName) == 0) {
        printf("%s : %s\n", help_entries[i].command, help_entries[i].description);
        return;
      }
    }
    // Si la commande n'est pas trouvÃ©e
    fprintf(stderr, "Erreur : commande '%s' non reconnue.\n", cmdName);
  }

}






/**
 * MÃ©thode qui ajoute var comme variable d'environnement
 */
void exportVariable(char* var) {

  // Utilise setenv pour dÃ©finir la variable d'environnement
  char *equal_sign = strchr(var, '=');

  if (equal_sign != NULL) {
    *equal_sign = '\0'; // SÃ©pare le nom de la valeur
    setenv(var, equal_sign + 1, 1); // DÃ©finir la variable d'environnement
  }
  else {
    fprintf(stderr, "Erreur : Format incorrect pour l'exportation de la variable.\n");
  }

}






/**
 * MÃ©thode qui supprime une variable donnÃ©e (rÃ©ciproque de exportVariable(char* var))
 */
void unsetVariable(char* var) {

    if (var == NULL || *var == '\0') {
        fprintf(stderr, "Erreur : nom de variable invalide.\n");
        return;
    }

    // VÃ©rifier si le nom contient un signe '=' (optionnel, pas nÃ©cessaire pour unset)
    if (strchr(var, '=') != NULL) {
        fprintf(stderr, "Erreur : le nom de la variable ne doit pas contenir de '='.\n");
        return;
    }

    // Supprimer la variable d'environnement
    if (unsetenv(var) != 0) {
        perror("Erreur lors de la suppression de la variable");
    }

}







/**
 * MÃ©thode qui cherche les variables prÃ©cÃ©dÃ©es d'un `$` et les remplace par leur valeur
 */
void lookForVariables() {

    int i = 0;
    // Parcourt le tableau d'arguments jusqu'Ã  rencontrer NULL
    while (args[i] != NULL) {

        // VÃ©rifie si l'argument commence par un dollar ($)
        if (args[i][0] == '$') {
            // RÃ©cupÃ¨re le nom de la variable (sans le $)
            char* var_name = args[i] + 1;

            // Recherche la valeur de la variable dans l'environnement
            char* value = getenv(var_name);

            // Si la variable est trouvÃ©e, remplace l'argument par sa valeur
            if (value != NULL) {
                args[i] = strdup(value); // Utilise strdup pour allouer une copie de la valeur
            }
            else {
              // Si la variable n'est pas trouvÃ©e, afficher un message
              printf("%s", "Erreur : Une variable n'a pas Ã©tÃ© trouvÃ©e dans l'environnement.\n");
            }

        }

        i++;

    }

}








/**
 * MÃ©thode qui gÃ¨re les commandes built-in
 * @return 1 si une commande built-in est effectuÃ©e, 0 sinon
 */
int handleBuiltInCmd() {

  // Checks si la commande est une commande built-in
  if (strcmp(args[0], "cd") == 0) {
    changeDirectory(args[1]);
    return 1;
  }
  if (strcmp(args[0], "export") == 0) {
    exportVariable(args[1]);
    return 1;
  }
  if (strcmp(args[0], "unset") == 0) {
    unsetVariable(args[1]);
    return 1;
  }
  if (strcmp(args[0], "help") == 0) {
    help(args[1]);
    return 1;
  }
  if (strcmp(args[0], "exit") == 0) {
    exit(0);
  }

  return 0;

}






/**
 * MÃ©thode qui execute la commande de l'utilisateur
 */
void execute(char** envp) {

  // CrÃ©ation du processus fils
  int pid = fork();

  if (pid == 0) { // Processus fils
    char* path = args[0]; // Commande brute

    // Test si la commande brute est directement exÃ©cutable
    if (access(path, X_OK) != 0) {

      // Commande non trouvÃ©e directement, recherche dans le PATH
      char *env_path = getenv("PATH"); // RÃ©cupÃ©ration du PATH de l'environnement
      if (env_path != NULL) { // VÃ©rification que PATH n'est pas NULL
        char path_buffer[MAXLI]; // Buffer pour construire le chemin complet
        char *dir = strtok(env_path, ":"); // DÃ©coupage du PATH en rÃ©pertoires

        while (dir != NULL) {
          // Construction du chemin complet
          snprintf(path_buffer, sizeof(path_buffer), "%s/%s", dir, args[0]);

          // Test si le chemin existe bien et est accessible
          if (access(path_buffer, X_OK) == 0) {
            path = path_buffer;
            break;
          }

          dir = strtok(NULL, ":"); // Passage au rÃ©pertoire suivant
        }
      }

    }

    // ExÃ©cution de la commande
    execve(path, args, envp);  // Passe le tableau d'environnement mis Ã  jour
    perror("execve failed"); // Si execve Ã©choue pour une raison quelconque
    exit(EXIT_FAILURE);

  }
  else if (pid > 0) { // Processus parent
    wait(NULL); // Attente de la fin du processus fils
  }

}






/**
 * MÃ©thode qui divise la commande donnÃ©e par l'utilisateur en sÃ©parant la commande et ses arguments
 * @return Un tableau contenant la commande (index 0) et ses arguments (autres index)
 */
char** parseArguments(char* cmd) {

  // Suppression du caractÃ¨re de nouvelle ligne
  cmd[strcspn(cmd, "\n")] = 0;

  // Analyse de la commande et des arguments
  int i = 0;

  // Utilisation de strtok pour sÃ©parer les arguments
  char *token = strtok(cmd, " "); // DÃ©coupage de la commande en mots sÃ©parÃ©s par des espaces
  while (token != NULL) { // Tant qu'il reste des mots
    args[i++] = token; // Ajout du mot dans le tableau d'arguments permet de garder en mÃ©moire les arguments ex :ls -l
    token = strtok(NULL, " ");
  }
  args[i] = NULL;

  if (args[0] == NULL) {
    fprintf(stderr, "Erreur : commande vide.\n");
    return NULL;
  }

  return args;

}






/*
 * MÃ©thode qui execute la commande passÃ©e en paramÃ¨tre
 */
void mbash(char *cmd, char** envp) {

  // Gestion des arguments
  strcpy(*args,*parseArguments(cmd));

  // Si la commande donnÃ©e par l'utilisateur contient une variable, la variable ($x) est changÃ©e par sa valeur
  lookForVariables();

  if (args != NULL) {

    int executed = handleBuiltInCmd();

    // On execute la commande si ce n'est pas une commande built-in
    if (executed == 0) {
      execute(envp);
    }

  }

}






/*
 * MÃ©thode principale
 */
int main(int argc, char** argv, char** envp) {

    // On rÃ©cupÃ¨re le chemin du .mbashrc
    char* file_name = "/.mbashrc";

    // Sauvegarde du chemin du home dans une variable globale
    home = getenv("HOME");

    // Construction du chemin complet pour .mbashrc
    mbashrc_path = malloc(strlen(home) + strlen(file_name) + 1); // Allocation mÃ©moire
    strcpy(mbashrc_path, home);           // Copie le chemin du HOME
    strcat(mbashrc_path, file_name);      // Ajoute "/.mbashrc" au chemin du HOME


    // Boucle infinie
    while (1) {
        // On affiche les informations de l'utilisateur
        char *info = getINFO();
        printf("%s", info);

        // On demande Ã  l'utilisateur de rentrer une commande bash
        fgets(cmd, MAXLI, stdin);

        // Si la commande donnÃ©e par l'utilisateur est un alias, on renvoie la commande associÃ©e
        strcpy(cmd, lookForAliases(cmd));

        // On traite la commande via l'automate
        process_with_automaton(cmd, envp);
    }

}
