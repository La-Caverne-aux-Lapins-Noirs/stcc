# Arguments de main sous TOS

Les deux entrées suivantes sont prises en charge :

```c
int main(void);
int main(int argc, char *argv[]);
```

`char **argv` est équivalent à la seconde forme. L'ABI reste celle de stcc :
`argc` occupe deux octets, `argv` quatre, et les paramètres arrivent sur la pile.
Les programmes existants avec `main(void)` ignorent simplement ces paramètres.
Le démarrage ROM appelle également `main` avec `argc == 0` et un tableau `argv`
contenant uniquement son pointeur nul final : il n'y a pas de shell dans la ROM.

## Ligne de commande classique

TOS fournit une basepage dont l'octet à `$80` donne la longueur de la ligne,
suivie des caractères. stcc en copie au plus 126 dans son propre tampon, puis
prépare les arguments avant `main`. La ligne originale reste accessible dans
`stcc_basepage->command_line`, sans modification par le parseur.

Le contrat de découpage de stcc est le suivant :

- Les espaces et tabulations séparent les arguments hors guillemets.
- Les guillemets simples ou doubles regroupent des caractères et sont retirés.
- `""` et `''` produisent chacun un argument vide.
- Dans une portion entre guillemets, doubler le même guillemet en produit un
  littéral : `"a""b"` devient `a"b`.
- L'antislash reste un caractère ordinaire, pour conserver les chemins Atari.
- Une citation non fermée se termine à la fin de la ligne ; CR et NUL arrêtent
  aussi la copie de la ligne courte.

Le format classique ne transmet pas le nom du programme. stcc fournit alors
`argc >= 1`, `argv[0]` égal à une chaîne vide modifiable et `argv[argc] == 0`.
Ne pas inventer le chemin du programme à partir de la basepage du parent.

```text
18ARGS.TTP alpha "deux mots" "" C:\TMP\A.TXT
```

L'exemple `18_args.c` affiche quatre lignes, délimitées par des crochets.
Les jokers, variables d'environnement, redirections et pipes sont l'affaire
du shell ; le runtime ne les développe pas.

## Extension Atari ARGV

Le protocole historique est activé seulement lorsque la longueur dans la
basepage vaut 127 **et** que l'environnement contient une entrée `ARGV=`.
La valeur éventuelle de cette entrée est ignorée. Les chaînes NUL suivantes
sont déjà séparées : nom du programme, puis arguments, jusqu'au NUL final.
stcc les utilise directement, sans redécouper espaces et guillemets.

La longueur cumulée n'est alors pas limitée à 126 caractères. Jusqu'à 64
arguments, le tableau de pointeurs emploie la réserve statique. Au-delà,
GEMDOS `Malloc` est appelé après `Mshrink` ; TOS libère ce bloc à la terminaison
du processus. La limite d'`argc` signé est 32767, mémoire disponible comprise.
Un échec de réservation ou un nombre supérieur termine le programme avec
le code 127, avant `main`, sans troncature silencieuse.

Une variable `ARGV=` isolée sans le marqueur 127 ne remplace pas la ligne
courte. Un marqueur sans environnement ARGV exploitable revient à cette ligne.
La convention supplémentaire `ARGV=NULL:...` pour coder des arguments vides
n'est pas interprétée : seule la forme historique par chaînes NUL est couverte.
La liste d'environnement reste disponible via la basepage, sans modification.

## Démarrage et installation

`runtime/args.c` est compilé par stcc pendant la liaison TOS automatique, puis
lié avec le démarrage et les primitives entières. Aucun compilateur Atari ni
aucune libc externe n'est nécessaire. Ses inclusions et macros restent celles
du support fourni, indépendamment des `-I` et `-D` du programme utilisateur.

`<stcc.h>` expose aussi `stcc_argc` et `stcc_argv` aux autres fonctions. Ces
symboles, comme `stcc_basepage`, appartiennent au démarrage TOS. Le démarrage
ROM ne les fournit pas. Avec `--nostartfiles`, l'auteur du programme reste
responsable de son entrée et peut choisir de lier ce support manuellement.

La source `.c` du support fait partie de l'installation à `share/stcc/runtime/`.
Les objets STCO restent en version 1 ; un objet ancien avec `main(void)` peut
être lié avec le nouveau démarrage.

## Vérifications

```sh
make test examples
python3 tests/arguments_hatari.py --hatari hatari --tos /chemin/etos192us.img
```

La suite CPU essaie deux adresses de chargement, les séparateurs, citations,
chemins, limites courtes, ARGV long, allocation et erreur d'allocation. La suite
Hatari lance un parent qui appelle réellement GEMDOS `Pexec` pour ses enfants,
puis vérifie le retour au parent. Le debugger observe leur terminaison ; il
ne fabrique ni les arguments ni l'environnement de l'enfant.
