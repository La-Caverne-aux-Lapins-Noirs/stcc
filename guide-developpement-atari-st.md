# Développer sur Atari ST quand on vient du GNU C sous GNU/Linux

**Guide conceptuel — programmes PRG/TOS/TTP, Atari ST à 68000, stcc 0.6.0**

Ce guide s’adresse à quelqu’un qui sait déjà programmer en C et qui utilise habituellement GCC, la glibc et les services de GNU/Linux. Il explique les habitudes qui deviennent trompeuses sur ST, en particulier lorsque deux opérations portent presque le même nom.

La cible principale est **TOS classique sur le ST original**. FreeMiNT, MagiC et les machines ultérieures ajoutent des possibilités : leur présence dans une documentation Atari ne garantit pas leur disponibilité sur cette cible. Un émulateur reproduit le système choisi ; lancer Hatari avec TOS ne transforme pas TOS en Linux.

Les particularités de **stcc 0.6.0** sont signalées explicitement. Elles ne constituent pas des limitations générales du développement Atari. Une ROM autonome sans TOS est encore un autre environnement : elle ne dispose pas des services GEMDOS présentés ici.

## Sommaire

- [1. Repères](#1-repères)
- [2. Mémoire](#2-mémoire)
- [3. Fichiers](#3-fichiers)
- [4. Erreurs et errno](#4-erreurs-et-errno)
- [5. Volatile et interruptions](#5-volatile-et-interruptions)
- [6. Les types qui changent les contrats](#6-les-types-qui-changent-les-contrats)
- [7. Organiser un programme](#7-organiser-un-programme)
- [8. Exercices de lecture](#8-exercices-de-lecture)
- [9. Sources et périmètre de vérification](#9-sources-et-périmètre-de-vérification)

## 1. Repères

### Les services familiers viennent de plusieurs endroits

Sur GNU/Linux, on finit facilement par appeler « le C » un ensemble bien plus large que le langage.

| Élément utilisé | Qui en fournit le contrat ? | Conséquence pour un portage ST |
|---|---|---|
| Types, expressions, durée de vie d’une variable, `volatile` | Langage et implémentation du compilateur | Le langage reste familier ; tailles et détails de cible doivent être vérifiés. |
| `malloc`, `free`, `FILE`, `fopen`, `errno`, `exit` | Bibliothèque C | Ils demandent une bibliothèque cible et son support de démarrage. |
| `open`, `read`, `write`, `fork`, conventions des chemins Unix | Interfaces POSIX et système Unix | Leur disponibilité ne découle pas de la présence d’un compilateur C. |
| `Malloc`, `Fread`, `Pexec`, `Pterm` | GEMDOS | Ce sont des services de TOS, avec leurs propres conventions. |
| Écran, périphériques, vecteurs et services matériels Atari | BIOS, XBIOS, matériel ; AES/VDI pour GEM | Les responsabilités et les règles d’appel diffèrent suivant la couche. |

Un binding C pour GEMDOS construit l’appel système et expose son résultat. Une bibliothèque peut ajouter des tampons, convertir les erreurs, fournir un allocateur ou émuler une interface Unix. **Un prototype ressemblant à celui de Linux ne prouve pas que ce travail existe.**

Dans stcc 0.6.0, le démarrage et quelques en-têtes sont fournis, mais aucune libc Atari complète. Il n’y a donc pas automatiquement de `malloc`, de `stdio`, de `errno` ou de POSIX. Les noms `tos_open`, `tos_read` et `tos_close` employés plus loin désignent des bindings à écrire ou à fournir : ce guide précise leur contrat, sans supposer qu’ils soient installés.

### Monotâche ne signifie pas absence d’activité asynchrone

Sous TOS classique, le lancement normal d’un enfant suspend le parent jusqu’à son retour. Les interruptions matérielles continuent pourtant d’arriver. Du code résident et des callbacks peuvent intervenir ; les accessoires GEM ont aussi leur modèle d’exécution coopératif.

Deux habitudes Unix doivent donc être réexaminées séparément : l’idée d’un processus isolé des autres et celle d’un flot C qui serait seul à toucher ses données. Sur le ST original, l’absence d’isolation mémoire rend surtout les erreurs plus visibles pour l’ensemble de la machine.

## 2. Mémoire

### 2.1. Une adresse accessible n’est pas une allocation

Sur GNU/Linux, un programme manipule normalement des adresses virtuelles dans son espace de processus. Sur le ST original, les programmes et le système partagent un espace physique sans la protection mémoire d’un processus Linux ordinaire.

Cela ne donne pas au programme le droit d’utiliser toute adresse qui semble fonctionner. La mémoire appartient toujours à quelqu’un : TOS, programme courant, parent, écran, extension résidente… Une écriture hors limites peut modifier ces données sans provoquer immédiatement une faute facile à attribuer.

Une allocation reste donc un contrat de propriété. Il faut connaître son début, sa taille, sa durée de vie et la couche qui peut la libérer. `volatile` et le mode superviseur ne remplacent aucun de ces éléments.

Une réservation GEMDOS positive prélève un bloc de RAM disponible. On ne raisonne pas en espace virtuel extensible, en swap ou en croissance transparente de pile. Le budget mémoire du programme doit inclure ses données, ses tampons, sa pile et la place nécessaire aux enfants qu’il veut lancer.

### 2.2. Le paradoxe du démarrage : beaucoup de mémoire attribuée, aucune mémoire libre

Le chargement TOS réserve normalement au programme un grand bloc correspondant au plus grand espace libre disponible. Sa **basepage** décrit notamment les segments TEXT, DATA et BSS, ainsi que l’environnement. Le programme doit rendre l’excédent avec `Mshrink` pour permettre d’autres allocations et le chargement d’enfants. [S1]

Un programme qui n’a pas fait cette restitution peut constater qu’un appel à `Malloc` échoue alors qu’il n’utilise presque rien. La RAM n’a pas disparu : GEMDOS la considère déjà comme attribuée au grand bloc du programme.

**Dans stcc**, le démarrage normal réserve la pile, restitue l’excédent et prépare les arguments avant d’appeler `main`. Il ne faut pas refaire ce calcul dans chaque application. Un démarrage personnalisé avec `--nostartfiles` reprend cette responsabilité, y compris celle de conserver assez de place pour la pile.

Rendre de la mémoire qui contient encore des variables ou une pile active autoriserait GEMDOS à la réattribuer. « Le programme fonctionne encore » ne prouverait alors plus rien.

### 2.3. `Malloc` et `malloc` : deux niveaux de gestion

`Malloc` est l’allocateur de blocs de GEMDOS. Le `malloc` d’une bibliothèque C peut obtenir quelques gros blocs de cet allocateur, puis y gérer de nombreux petits objets. Un `free` peut simplement remettre un objet dans les réserves de la bibliothèque, sans appeler immédiatement `Mfree`.

Cette distinction existe aussi sur GNU/Linux : chaque `malloc` ne correspond pas nécessairement à un nouvel appel système. Sur les anciens GEMDOS, le nombre de blocs système est en plus limité. Une allocation système par petit maillon de liste est donc un choix particulièrement coûteux. [S2]

Pour un utilitaire, une stratégie raisonnable consiste à réserver un ou quelques blocs, puis à y gérer ses objets. Pour des objets qui vivent tous le temps d’une commande, une arène réinitialisée en une fois simplifie aussi la propriété. Pour une longue session de shell, les allocations propres au shell doivent pouvoir être réutilisées ou libérées entre deux commandes.

| Ressemblance | Contrat à retenir |
|---|---|
| `Malloc` ressemble à `malloc` | Pour une taille positive, il rend un bloc ou un pointeur nul. Il ne fournit pas à lui seul toute la bibliothèque C. |
| `Mfree` ressemble à `free` | On lui fournit le début d’un bloc système. Un sous-bloc distribué par un allocateur maison doit revenir à cet allocateur. |
| `free(NULL)` est autorisé en C | Un adaptateur C doit traiter ce cas ; ce contrat ne justifie pas un appel brut aveugle à `Mfree(0)`. |
| Une allocation ressemble à de la mémoire « neuve » | Son contenu n’est pas garanti nul. Initialiser les objets reste nécessaire. |
| `Malloc(-1L)` semble donner la RAM libre | Il interroge la taille du plus grand bloc disponible, pas la somme de tous les espaces libres. |

La fragmentation explique ainsi qu’une demande puisse échouer malgré une quantité totale de mémoire apparemment suffisante. Deux allocations successives n’ont aucune obligation d’être voisines.

### 2.4. `Mshrink` n’est pas un `realloc` général

Sous TOS classique, `Mshrink` réduit un bloc existant **sur place**. On lui indique sa **nouvelle taille totale**, pas le nombre d’octets à retirer. Réduire un bloc de 20000 à 12000 octets signifie demander 12000, pas −8000.

L’adresse reste la même ; le résultat est un statut. Ce n’est ni une interface de déplacement du bloc, ni un moyen général de l’agrandir. Des extensions de systèmes ultérieurs ne font pas partie du contrat de base. [S2]

Pour construire un `realloc` capable d’agrandir, une bibliothèque peut devoir réserver ailleurs, copier puis libérer l’ancien bloc. En cas d’échec de la nouvelle réservation, l’ancien objet doit rester disponible. Le pic de mémoire nécessaire peut donc dépasser largement la taille finale souhaitée.

### 2.5. Qui récupère les blocs à la fin ?

La propriété d’un bloc est enregistrée par le système ; elle ne dépend pas de l’existence d’une variable C qui conserve son adresse. Les descripteurs internes de GEMDOS comportent un propriétaire. La terminaison normale récupère les blocs appartenant au processus. [S3, S4]

Perdre le dernier pointeur crée tout de même une fuite **pendant l’exécution**. Pour un shell qui reste en mémoire des heures, cette durée est précisément celle qui compte. La récupération finale n’excuse donc pas l’absence de gestion des objets à courte durée de vie.

Le lancement d’un enfant illustre l’importance du propriétaire :

| Mode de lancement | Responsabilité mémoire essentielle |
|---|---|
| `Pexec(0)` : charger et exécuter | Choix ordinaire d’un shell simple ; le parent attend, et la terminaison normale nettoie les blocs appartenant à l’enfant. |
| `Pexec(3)` puis `Pexec(4)` | Les blocs de chargement — image avec basepage et environnement créé pour elle — restent à la charge du parent. Il conserve leurs adresses pour les libérer. Les allocations ensuite faites pour le compte de l’enfant suivent leur propre propriété. |

Le second cas ne demande pas au parent de retrouver tous les pointeurs internes du programme enfant. Il lui demande de conserver les blocs qu’il a lui-même fait charger. Ce découpage sert notamment aux chargeurs particuliers et aux débogueurs. [S4]

### 2.6. Résident, global et permanent ne sont pas synonymes

Une variable C `static` ou globale vit pendant l’exécution du programme. Elle ne survit pas par magie à sa terminaison.

Pour laisser du code installé après la fin du programme, TOS possède notamment `Ptermres`, la terminaison avec maintien en résidence. La mémoire retenue est retirée des listes ordinaires de GEMDOS : sous TOS classique, ce n’est pas un bloc qu’un futur `Mfree` permettra simplement de récupérer. [S4]

L’usage est légitime pour un service résident, mais il demande un véritable protocole de durée de vie. Un vecteur d’interruption qui pointe encore dans le code d’un PRG déjà libéré est un pointeur pendant au niveau du système entier.

GEM ne doit pas être présenté comme disposant d’un second `malloc` public « permanent ». Les fenêtres, ressources et stations graphiques ont leurs propres contrats ; les extensions résidentes ont les leurs. De même, un mode « global » d’une API mémoire étendue concerne l’accès à la mémoire et ne signifie pas automatiquement « conservé après la mort de son propriétaire ».

### 2.7. Pile et nettoyage système

La pile de stcc fait **16 Kio par défaut**, configurables avec `--stack-size`. Elle n’a pas de garde de débordement. Un tableau local de plusieurs dizaines de kilooctets peut donc écraser autre chose, même si la fonction ne compte que dix lignes.

`Pterm` termine le processus au niveau GEMDOS. Le rapprocher de `exit` aide à comprendre le contrôle d’exécution, mais il ne faut pas lui attribuer les traitements d’une libc : vidage des tampons `stdio`, callbacks `atexit`, etc. Dans stcc actuel, le retour de `main` aboutit directement au support de terminaison TOS.

Enfin, libérer la RAM n’annule pas toutes les actions du programme. Il faut retirer ses callbacks avant de libérer leur code, restaurer les vecteurs et l’état matériel modifiés, et fermer les ressources des bibliothèques utilisées.

## 3. Fichiers

### 3.1. Handle GEMDOS, descripteur Unix et `FILE *`

Un handle GEMDOS est un identifiant entier utilisé par le système. Un `FILE *` représente l’état d’un flux géré par une bibliothèque C : tampon, indicateurs d’erreur et de fin, éventuelles conversions de texte. Ces objets ne sont pas interchangeables.

Le piège le plus immédiat concerne les numéros familiers :

| Handle GEMDOS standard | Destination habituelle sous TOS classique |
|---|---|
| `0` | Entrée standard |
| `1` | Sortie standard |
| `2` | Périphérique auxiliaire, normalement série |
| `3` | Imprimante |

**Le handle brut `2` n’est pas le `stderr` de Linux.** Une bibliothèque C ou un shell peut établir une autre convention ; il faut la connaître. Les nouveaux handles de fichiers ordinaires de TOS classique sont normalement attribués à partir de 6. [S5]

Un handle se traite selon la convention de résultat de la fonction, pas comme un pointeur. Tester `if (!handle)` n’est pas une détection générale d’erreur. Tester seulement `== -1` n’est pas davantage correct pour un retour GEMDOS brut.

La présence d’une entrée et d’une sortie standard n’implique pas non plus des pipes Unix ou un `/dev` universel. Pour qu’un utilitaire respecte une redirection, il doit écrire vers le canal convenu avec le lanceur ; dessiner directement à l’écran contourne cette redirection.

### 3.2. Lire des octets n’est pas lire une chaîne

Pour un fichier disque ordinaire, un binding qui conserve le contrat brut de `Fread` renvoie : une valeur négative en cas d’erreur ; zéro en fin de fichier lorsqu’aucun octet n’est obtenu ; une valeur positive pour le nombre d’octets transférés, éventuellement inférieur à la demande. Aucun `NUL` de chaîne C n’est ajouté. `Fwrite` renvoie lui aussi un compte réel ou une erreur. [S6]

Une copie doit donc utiliser le compte obtenu, pas la capacité du tampon. Une écriture courte n’est pas une réussite complète. Si l’on choisit de poursuivre, on avance du compte réellement écrit ; zéro octet écrit alors qu’il en reste exige une décision d’arrêt, pas une boucle infinie.

Ce réflexe est déjà nécessaire avec `read` et `write` sous Linux. La différence est la convention d’erreur et l’absence de libc implicite, pas une permission nouvelle d’ignorer les transferts partiels.

Exemple avec des bindings **hypothétiques**, qui rendent les résultats GEMDOS sans les convertir en `errno`. Le tampon fourni doit contenir au moins 1024 octets. Le résultat est un compte, ou un code négatif ; la première erreur est conservée lors du nettoyage.

```c
extern long tos_open(const char *chemin, int mode);
extern long tos_read(int handle, long taille, void *tampon);
extern long tos_close(int handle);

long lire_debut(const char *chemin, unsigned char *tampon)
{
    long resultat;
    long fermeture;
    int handle;

    resultat = tos_open(chemin, 0);
    if (resultat < 0)
        return resultat;
    handle = (int)resultat;
    resultat = tos_read(handle, 1024L, tampon);
    fermeture = tos_close(handle);
    if (resultat < 0)
        return resultat;
    if (fermeture < 0)
        return fermeture;
    return resultat;
}
```

Cette fonction lit un début de fichier, pas nécessairement un enregistrement complet. Si un format exige exactement 1024 octets, cette exigence appartient au niveau qui interprète le format. Un fichier vide n’est pas automatiquement une erreur système.

Pour cibler les premiers TOS, les bindings doivent aussi absorber certaines irrégularités historiques : avant GEMDOS 0.15, une longueur nulle passée directement à `Fread` ou `Fwrite` peut bloquer le système. Un adaptateur dont le contrat est « transférer zéro octet ne fait rien » peut rendre immédiatement zéro. [S7]

### 3.3. Chemins et noms : plusieurs lecteurs, plusieurs répertoires courants

Le modèle classique utilise des lecteurs et des noms courts 8.3, sans distinction de casse pour les noms usuels du système de fichiers TOS. La portabilité vers des fichiers longs sous MiNT est une extension à prévoir séparément.

Il faut distinguer un lecteur courant et un répertoire courant associé à chaque lecteur. [S5, S8]

| Chemin Atari | Interprétation |
|---|---|
| `C:\JEUX\SCORE.DAT` | Chemin absolu sur C: |
| `C:SCORE.DAT` | Dans le répertoire courant de C: |
| `\SCORE.DAT` | À la racine du lecteur courant |
| `SCORE.DAT` | Dans le répertoire courant du lecteur courant |

Dans une chaîne C, l’antislash s’écrit `\\` : `"C:\\JEUX\\SCORE.DAT"`. Le lecteur, le répertoire courant, le dossier du programme et le dossier contenant ses ressources sont des notions distinctes. Il n’existe pas de `/tmp` universel à présumer.

`argv[0]` n’est pas non plus un moyen portable de retrouver l’exécutable. La ligne de commande classique de TOS ne transmet pas son nom : dans ce cas, stcc fournit une chaîne vide comme premier argument. L’extension ARGV peut transmettre un nom, mais la recherche des ressources doit toujours suivre une convention explicite avec le lanceur.

### 3.4. Texte, structure binaire et sauvegarde

Les appels de fichiers bruts transportent des octets. Les fichiers texte Atari emploient couramment CR/LF ; écrire `\n` ne demande pas à GEMDOS de fabriquer `\r\n`. Une bibliothèque C peut proposer un mode texte qui traduit les fins de ligne : cela dépend de cette bibliothèque. L’encodage des caractères demande la même décision explicite ; des octets UTF-8 ne deviennent pas automatiquement des caractères Atari.

Écrire directement une `struct` n’est pas un format portable : largeur des types, ordre des octets et padding peuvent changer entre PC et ST. Cela reste vrai même si `Fwrite` a transféré exactement le compte demandé. Pour un fichier partagé, définir les tailles et l’ordre des octets, puis encoder et décoder les champs.

`Fcreate` peut tronquer un fichier existant. Une sauvegarde robuste doit donc être pensée avant d’ouvrir l’ancien document en création. Un fichier temporaire distinct, des transferts vérifiés, une fermeture vérifiée puis une politique de remplacement et de sauvegarde de l’ancien contenu sont des choix utiles. [S6]

Ne transposez pas sans vérification les garanties Unix de remplacement par `rename`, de fichier encore accessible après `unlink`, de liens ou de permissions. Un renommage et une séquence « supprimer puis renommer » n’ont notamment pas les mêmes conséquences en cas d’interruption. Le modèle Linux de remplacement atomique n’est pas un contrat que le nom `Frename` suffit à établir. [S7, S9]

Sur une disquette, conserver une copie précédente peut être plus utile que d’imiter imparfaitement un protocole pensé pour un système de fichiers moderne. Cela améliore la récupération ; cela ne transforme pas plusieurs opérations FAT en transaction résistante à une coupure de courant.

### 3.5. La DTA : une recherche avec état implicite

`Fsfirst` et `Fsnext` utilisent une **DTA**, une zone mémoire sélectionnée pour le processus. Elle contient le résultat et l’état nécessaire à la poursuite de la recherche. Ce n’est pas un objet `DIR *` indépendant passé à chaque opération. [S5, S7]

Une fonction qui énumère des fichiers doit sauvegarder la DTA courante, sélectionner la sienne puis restaurer l’ancienne sur tous ses chemins de sortie. Une recherche imbriquée doit protéger son propre contexte de la même manière. Changer seulement le nom recherché sans changer de contexte peut détruire l’itération en cours.

La durée de vie de la zone compte également : sélectionner une DTA locale puis quitter la fonction sans restauration laisse à TOS l’adresse d’un objet de pile disparu.

Enfin, la DTA initiale recouvre la zone de ligne de commande dans la basepage. Utiliser directement cette zone pour une recherche peut écraser la ligne brute. **stcc prépare déjà ses arguments avant `main`** ; cela ne constitue pas une protection de la ligne brute que l’application voudrait relire plus tard.

## 4. Erreurs et errno

### 4.1. `errno` appartient à une interface de bibliothèque

Sous GNU/Linux, l’appel C `read` habituel passe par la bibliothèque : en cas d’échec système, elle fournit le résultat prévu par son interface, généralement `-1`, et renseigne `errno`. L’interface brute avec le noyau et la fonction de bibliothèque ne doivent pas être confondues. [S10]

GEMDOS fournit généralement ses propres codes négatifs dans le résultat de l’appel. Il n’existe pas une variable C universelle que chaque TRAP renseignerait. Ajouter soi-même `int errno;` ne change donc rien au comportement des appels système.

Exemple concret : un fichier absent peut produire `-33`, nommé historiquement `EFILNF`. Une fonction qui ne teste que `resultat == -1` laissera alors passer l’erreur. Les autres erreurs, telles que chemin absent ou accès refusé, ont leurs propres codes. [S11]

Une bibliothèque peut choisir d’adapter ce résultat : `-33` devient alors une sentinelle `-1`, accompagnée d’une valeur symbolique de type `ENOENT`. Elle doit définir cette conversion. Les valeurs numériques des constantes Linux ne doivent pas être recopiées pour la fabriquer.

### 4.2. Deux conventions cohérentes valent mieux qu’un mélange

Pour son projet, on peut choisir l’une des deux interfaces suivantes :

| Interface du projet | Détection et information d’erreur |
|---|---|
| Résultat GEMDOS conservé | Tester la convention de l’appel, puis conserver le code négatif utile au diagnostic. |
| Adaptateur de style libc | Tester la sentinelle documentée, puis consulter l’erreur définie par cette bibliothèque. |

Le mélange est la source du piège : conserver le retour brut, tester `== -1`, puis afficher un `errno` qui provient d’un autre appel.

Un code de retour explicite se transmet facilement à travers les couches. Il permet aussi de mémoriser l’erreur principale avant de fermer des fichiers ou de restaurer des ressources. Le résultat d’une opération de nettoyage ne doit pas effacer silencieusement la cause initiale de l’échec.

Tous les appels n’utilisent toutefois pas la même convention : `Malloc` signale par exemple son échec par un pointeur nul. Certaines valeurs renvoyées par des fonctions BIOS sont des données, pas des codes d’erreur. Il faut documenter le contrat de chaque famille, sans inventer une règle « tout résultat négatif de TOS est une erreur ».

### 4.3. Les mauvaises habitudes restent mauvaises sur Linux

`errno` n’est pas un compte rendu global de la dernière instruction C. On le consulte lorsque le contrat de l’opération l’exige. Une valeur non nulle peut subsister après une réussite ; tester seulement `errno` après un appel n’est donc pas une détection générale d’échec. Les fonctions dont les résultats sont ambigus demandent leur protocole particulier. [S12]

Sur GNU/Linux avec la glibc, `errno` est propre au thread et peut être une macro. Un éventuel `errno` maison pour un programme ST n’acquiert pas ces propriétés. Le déclarer `volatile` ne lui donne ni provenance fiable, ni isolation entre contexte principal et interruption. [S13]

**Dans stcc 0.6.0**, aucun `<errno.h>`, `perror` ou `strerror` cible n’est fourni. Un premier projet peut simplement transmettre ses erreurs explicitement et traduire quelques codes pour l’utilisateur. Une couche plus proche de la libc pourra venir ensuite, avec un contrat écrit.

### 4.4. Le retour d’erreur n’est pas toute l’interaction

Une erreur de disque ou une demande de changement de support peut passer par le gestionnaire d’erreur critique installé dans le système. Selon ce gestionnaire, une intervention de l’utilisateur ou une tentative de reprise peut précéder le retour à l’appelant. C’est une différence visible avec l’idée d’une opération qui se contenterait de rendre immédiatement un code et de renseigner `errno`. [S5]

Les essais doivent donc inclure les erreurs réelles du support émulé, pas seulement un nom de fichier inexistant sur un dossier partagé du PC.

## 5. Volatile et interruptions

### 5.1. Ce que le qualificatif exprime

Un objet ou un accès `volatile` indique au compilateur que les accès ont une importance qui dépasse les seuls calculs C qu’il voit. C’est notamment utile pour des registres matériels et, suivant le contrat du compilateur cible, pour des objets partagés avec du code d’interruption.

Une boucle d’attente qui semble ne jamais modifier son indicateur ne doit pas être raisonnée comme si personne d’autre ne pouvait le faire. Inversement, qualifier toutes les variables `volatile` ne rend pas le programme plus correct : on doit savoir quel accès est nécessaire et pourquoi.

| Écriture C | Ce qui est qualifié |
|---|---|
| `volatile int *p;` | L’entier accédé par `p` |
| `int * volatile p;` | La variable pointeur `p`, pas l’entier pointé |
| `volatile int * volatile p;` | Le pointeur et l’entier accédé |
| `const volatile int *p;` | Un entier que ce code lit sans le modifier, et dont les lectures restent significatives |

`const volatile` convient par exemple à une vue en lecture d’un registre qui change matériellement. Ce n’est pas contradictoire : `const` limite les écritures par cette vue C, sans figer le périphérique.

### 5.2. Un registre matériel n’est pas une variable ordinaire

Une lecture peut acquitter un événement ; une écriture peut lancer une opération. La largeur de l’accès, l’adresse et le nombre d’accès font partie du protocole du périphérique. Une lecture supplémentaire « pour vérifier » peut donc modifier son état.

Le type choisi doit correspondre à ce protocole. Exemple générique de deux accès sur **un octet**, à des adresses qui doivent avoir été choisies et rendues accessibles correctement par le code de plateforme :

```c
unsigned char lire_port(volatile const unsigned char *port)
{
    return *port;
}

void ecrire_port(volatile unsigned char *port, unsigned char valeur)
{
    *port = valeur;
}
```

Remplacer le type par `unsigned long` pour aller « plus vite » peut toucher d’autres registres. Employer `|=` impose une lecture suivie d’une écriture au niveau C ; cela n’est correct que si le matériel autorise cette opération. Les champs de bits C sont également un mauvais choix par défaut pour décrire des registres : ils peuvent introduire des accès et des opérations de lecture-modification-écriture difficiles à contrôler. [S14]

Sur 68000, les accès mot et long mot demandent une adresse paire. Et les registres matériels du ST ne deviennent pas accessibles en mode utilisateur parce que le pointeur est `volatile` : les privilèges sont une question distincte. [S15]

### 5.3. `volatile` ne rend pas une opération atomique

Considérons un compteur partagé :

```c
volatile unsigned int compteur;

void incrementer(void)
{
    compteur++;
}
```

Le compilateur doit traiter l’accès comme significatif. Cela ne promet pas une instruction matérielle unique pour l’incrément. stcc 0.6.0 produit pour ce cas une lecture, un calcul et une écriture séparés.

Si le programme principal et une interruption incrémentent tous deux ce compteur, une mise à jour peut être perdue : le principal lit 10 ; l’interruption écrit 11 ; le principal reprend son ancien calcul et écrit lui aussi 11. Deux événements ont produit un seul incrément visible.

Pour un indicateur, un autre piège consiste à tester « travail en attente », puis à le remettre à zéro. Une interruption survenant entre le test et l’effacement peut voir sa notification effacée. Le choix dépend alors du besoin : fusionner des demandes identiques est parfois voulu ; compter chaque occurrence exige un protocole plus précis.

Une solution possible est une section critique très courte qui copie et efface l’état partagé en bloquant les interruptions concernées, puis **rétablit le masque précédent**. Il ne faut pas réactiver aveuglément ce qui était déjà masqué à l’entrée. L’accès au registre d’état et au mode superviseur doit être organisé par le code de plateforme ; `volatile` ne fournit pas cette primitive.

Le 68000 impose aussi une nuance utile : un transfert de 32 bits sur son bus de 16 bits ne signifie pas automatiquement qu’une interruption CPU ordinaire puisse couper une instruction `MOVE.L` en deux. Il faut distinguer instruction, cycles de bus et séquence générée pour une expression C. Une copie de structure ou plusieurs lectures peuvent néanmoins être interrompues entre instructions ; un autre acteur du bus a encore un autre contrat. [S15]

### 5.4. Visibilité, ordre et cohérence sont trois questions

Une valeur relue, une opération indivisible et un ensemble de données cohérent ne sont pas la même garantie.

Supposons qu’une interruption remplisse un tampon, puis lève un indicateur. Rendre uniquement l’indicateur `volatile` ne définit pas un mécanisme général de publication du contenu du tampon. GCC précise en particulier qu’un accès volatile n’est pas une barrière ordonnant tous les accès non volatils. [S14]

Pour partager un ensemble de données, il faut définir qui écrit, qui lit, quand une zone est disponible et comment elle est protégée pendant son utilisation. Un double tampon avec transfert de propriété ou une courte section critique peut être approprié. Ajouter des qualificatifs ne répond pas à ces questions de protocole.

De même, un verrou tournant naïf dans le programme principal peut être catastrophique si l’interruption attend ce verrou alors qu’elle a interrompu son détenteur. Le code qui pourrait le libérer ne reprendrait jamais. Les primitives des threads Linux ne se transposent donc pas mot pour mot à une interruption sur un processeur unique. [S16]

### 5.5. Un gestionnaire correct a plusieurs contrats

Dans stcc, `__interrupt__` sauvegarde les registres prévus et produit un retour d’exception `RTE`. Cela ne réalise ni l’installation du vecteur, ni l’acquittement du matériel, ni la protection des données partagées.

Toutes les fonctions appelées « callback » ne sont d’ailleurs pas des entrées d’exception. Un callback de la file VBL différée de TOS se termine comme une fonction ordinaire ; ce n’est pas le même contrat qu’un vecteur CPU direct. Employer `RTE` à la place de `RTS`, ou l’inverse, compromet le retour.

Pour un premier programme, un modèle utile consiste à limiter l’interruption à l’acquittement nécessaire et à la mise à jour d’un petit état, puis à faire le travail dans la boucle principale. En particulier, **GEMDOS n’est pas réentrant : on n’appelle pas l’allocation système ou les opérations de fichiers depuis un gestionnaire matériel**. Les appels AES doivent eux aussi rester dans leur contexte d’exécution approprié. [S17]

### 5.6. Ce qui est spécifique à stcc

La version 0.6.0 accepte `volatile` et émet actuellement les accès mémoire sans optimisations agressives. Une boucle mal qualifiée peut donc sembler fonctionner. Ce comportement actuel ne doit pas devenir le contrat implicite du code : une évolution du compilateur ou un portage vers GCC peut révéler l’erreur.

Le support de l’assembleur inline de stcc n’est pas l’assembleur étendu de GCC. Les contraintes et barrières écrites pour GCC ne se recopient pas mécaniquement. Pour une primitive sensible, définir son contrat puis regarder l’assembleur généré est une vérification utile.

Enfin, les termes se ressemblent sans désigner la même chose : un registre dit « volatil » dans une ABI peut être détruit par un appel ; le qualificatif C `volatile` concerne les accès ; `register(d3)` choisit un stockage dans un registre CPU. Réserver D3 ne crée ni registre matériel mappé en mémoire, ni mécanisme de synchronisation.

Un compteur `volatile` utilisé dans une boucle vide ne devient pas non plus une horloge. Il peut empêcher la disparition de certains accès, mais la durée dépend encore du code produit et de la machine. Pour attendre un événement vidéo ou mesurer du temps, choisir une source temporelle adaptée plutôt qu’un nombre supposé constant d’itérations.

## 6. Les types qui changent les contrats

Les difficultés précédentes sont amplifiées par un modèle entier différent de celui d’un PC GNU/Linux courant.

| Type | stcc sur ST | GNU/Linux x86-64 courant, ABI LP64 |
|---|---|---|
| `char` | 8 bits, signé par défaut | 8 bits ; signedness dépendant de la cible et des options |
| `short` | 16 bits | 16 bits |
| `int` | 16 bits | 32 bits |
| `long` | 32 bits | 64 bits |
| Pointeur | 32 bits | 64 bits |
| `size_t` | `unsigned long`, 32 bits | Entier non signé de 64 bits |

Ce tableau décrit deux implémentations précises, pas des propriétés universelles de C. Les pointeurs de stcc ne sont pas limités à 16 bits parce que `int` l’est. Le ST original n’exploite toutefois pas 32 bits d’adressage physique.

### Le type du résultat ne répare pas le calcul

Cet exemple est incorrect sur stcc :

```c
long calcul_incorrect(void)
{
    return 320 * 200;
}
```

Les opérandes sont des `int` de 16 bits, et 64000 n’y est pas représentable comme entier signé. Le débordement survient avant la conversion vers le retour `long` ; ce n’est pas une simple troncature portable.

Choisir le type avant de multiplier :

```c
unsigned long nombre_pixels(void)
{
    return 320UL * 200UL;
}
```

Le même piège existe pour `nombre * octets_par_element` si ces deux variables sont des `int`. Avec l’opérateur `sizeof` lui-même, stcc fournit un `size_t` sur 32 bits ; cela ne dispense pas de vérifier le débordement d’une taille d’allocation, ni sa conversion vers la taille signée attendue par certains bindings.

### Tester le signe avant de changer de domaine

Conserver un résultat de `Fread` dans un `long` permet d’examiner son code d’erreur. Le convertir immédiatement en `unsigned long` transforme une erreur négative en un immense compte positif. Le réduire à `int` peut également détruire un compte valide supérieur à 32767.

Même vigilance pour les pointeurs : les stocker dans un `int` perd une partie de l’adresse. Une fonction implicitement déclarée comme retournant `int` peut tronquer une adresse ou un résultat système avant tout test. Avec stcc, les prototypes explicites sont donc particulièrement importants, même si ce guide s’intéresse surtout aux concepts.

### L’alignement n’est pas une option de performance

Lire un entier depuis `buffer + 1` par conversion vers un pointeur de mot ou de long peut provoquer une erreur d’adresse sur 68000. Pour un format binaire, reconstruire la valeur depuis des octets évite cette dépendance et rend l’ordre des octets explicite.

Les structures stcc demandent un alignement de deux octets et peuvent contenir du padding. La machine est big-endian. Aucune ressemblance entre déclarations C sur PC et ST ne garantit donc une représentation binaire identique.

## 7. Organiser un programme

Une architecture simple rend les différences visibles sans éparpiller les appels système partout.

1. **Logique du programme** : calculs, format des données, règles métier ; pas d’hypothèse sur le handle 2 ou sur une adresse matérielle.
2. **Couche de services** : fichiers, erreurs et allocation, avec un contrat écrit pour les retours, la propriété et les transferts partiels.
3. **Couche de plateforme** : bindings GEMDOS, installation des interruptions, sections critiques et accès matériels ; elle connaît les largeurs et les privilèges.

Un backend GNU/Linux et un backend TOS peuvent ainsi exposer les mêmes opérations utiles au projet, avec une sémantique réellement commune. Il n’est pas nécessaire de reproduire tout POSIX pour porter un éditeur, un jeu ou un petit shell.

Pour le cycle de vie d’un PRG ordinaire :

1. Laisser le démarrage préparer la pile et rendre la mémoire excédentaire.
2. Acquérir les ressources en mémorisant ce qui a effectivement réussi.
3. Exécuter la boucle principale ; y traiter les demandes remontées par les interruptions.
4. Arrêter et retirer les callbacks installés, puis restaurer l’état modifié.
5. Fermer les ressources, libérer les objets devenus inutiles et conserver le premier diagnostic pertinent.
6. Revenir de `main` avec un statut convenu avec le lanceur.

L’ordre exact suit les dépendances : un callback ne doit plus pouvoir s’exécuter lorsqu’on libère les données qu’il utilise. Cette règle vaut autant pour une adresse de DTA que pour un vecteur d’interruption.

Pour un shell, tester également l’enchaînement de nombreuses commandes. Une mesure après une seule terminaison ne montre pas une fuite dans le parent. `Pexec(0)` est le point de départ adapté au lancement synchrone ; employer les modes séparant chargement et exécution doit répondre à un besoin précis.

Pour les essais, distinguer un dossier du PC présenté par l’émulateur et une véritable image de disquette FAT. Le premier facilite le développement ; le second permet d’observer le comportement du système de fichiers et du support émulés. Vérifier aussi le programme avec la version de TOS visée : la réussite sous EmuTOS ne démontre pas à elle seule l’absence d’un problème propre aux premiers TOS.

Une ROM sans TOS conserve les règles de C et du matériel, mais reprend à son compte l’organisation de la RAM, les allocations et les services de fichiers éventuels. Les fonctions GEMDOS ne restent pas présentes parce que le code a été écrit avec le même compilateur.

## 8. Exercices de lecture

Pour chaque cas, identifier la couche dont on suppose à tort le contrat.

| Code ou situation | Diagnostic |
|---|---|
| `if (tos_open(nom, 0) == -1) ...` avec un retour GEMDOS brut | D’autres codes négatifs existent. Le handle obtenu doit aussi être conservé en cas de réussite. |
| `if (errno) ...` après un appel brut à `Fread` | Aucun adaptateur n’a nécessairement renseigné cette variable ; sous libc aussi, on suit d’abord le protocole d’erreur de l’appel. |
| `unsigned long n = tos_read(h, 512L, buf);` puis utilisation de `n` | La conversion efface le signe d’une éventuelle erreur avant le test. |
| `long n = 320 * 200;` sur stcc | Le calcul déborde en `int` signé de 16 bits avant l’affectation. |
| Une DTA locale est installée, puis la fonction retourne sans restauration | TOS conserve un pointeur vers un objet dont la durée de vie est terminée. |
| `volatile unsigned int n;` et `n++` dans le principal et l’IRQ | Le qualificatif ne protège pas la lecture-modification-écriture. |
| Écrire un diagnostic sur le handle GEMDOS `2` | On peut envoyer le message vers le port série au lieu du canal d’erreur attendu. |
| Passer à `Mfree` l’adresse d’un objet pris dans une arène | L’adresse n’est pas nécessairement celle d’un bloc appartenant à l’allocateur système. |
| Appeler directement `Pterm` en espérant vider une future bibliothèque `stdio` | Le service système ne remplace pas le nettoyage de la bibliothèque C. |
| Quitter normalement après avoir installé une interruption pointant dans le PRG | La RAM peut être récupérée alors qu’un vecteur la référence encore. |

Un bon exercice pratique consiste à écrire un petit copieur avec une convention d’erreur explicite, puis à le lancer depuis un shell minimal. Tester fichier vide, lecture courte, destination pleine, nom absent et répétition de dizaines de lancements. Ajouter ensuite une notification périodique par interruption, sans effectuer les entrées-sorties dans le gestionnaire.

## 9. Sources et périmètre de vérification

Les exemples C sont originaux. Les fonctions `tos_*` servent à expliciter un contrat ; elles ne constituent pas une nouvelle bibliothèque livrée avec stcc. L’exemple marqué incorrect est volontairement conservé pour expliquer les promotions entières.

Les particularités de stcc ont été vérifiées dans les sources et documents de la version 0.6.0 : `include/stddef.h`, `docs/ABI.md`, `docs/STATUS.md`, `docs/ARGUMENTS.md`, `docs/INTERRUPTS.md`, `docs/PORTABILITY.md`, `runtime/start.s` et le générateur de code. Le guide n’ajoute aucune fonctionnalité au compilateur.

Les quatre extraits C présentés comme corrects ont été vérifiés en compilation avec stcc et avec le compilateur hôte en mode C89 strict. Cette vérification n’est pas un essai matériel ; les bindings de fichiers restent à fournir pour lier et exécuter l’exemple correspondant.

Références techniques consultées le 19 octobre 2026. Les manuels Atari rassemblent souvent plusieurs générations de systèmes : lire les indications de disponibilité avant d’adopter une fonction.

- **[S1]** [tos.hyp — lancement d’un programme et TPA](https://freemint.github.io/tos.hyp/en/gemdos_tpa.html) : restitution de la mémoire excédentaire et place de la pile.
- **[S2]** [tos.hyp — gestion de la mémoire](https://freemint.github.io/tos.hyp/en/gemdos_memory.html) : `Malloc`, `Mfree`, `Mshrink`, extensions de `Mxalloc`.
- **[S3]** [tos.hyp — descripteur mémoire de TOS](https://freemint.github.io/tos.hyp/en/MD.html) : propriétaire d’un bloc.
- **[S4]** [tos.hyp — fonctions de processus](https://freemint.github.io/tos.hyp/en/gemdos_process.html) : propriété lors du chargement, terminaison normale et résidence.
- **[S5]** [The Atari Compendium — vue d’ensemble GEMDOS](https://bus-error.nokturnal.pl/atari_compendium/html/chap2.htm) : handles standard, noms, DTA et propriété des allocations.
- **[S6]** [tos.hyp — fonctions de fichiers](https://freemint.github.io/tos.hyp/en/gemdos_file.html) : comptes d’octets, création, recherches et conventions des appels. Les fonctions MiNT qui figurent sur cette page ne sont pas toutes disponibles sous TOS classique.
- **[S7]** [The Atari Compendium — référence GEMDOS](https://bus-error.nokturnal.pl/atari_compendium/html/gemdos.htm) et [errata](https://bus-error.nokturnal.pl/atari_compendium/html/errata.htm) : irrégularités des anciens GEMDOS, DTA, renommage et terminaison. Tenir compte des errata pour les bindings assembleur.
- **[S8]** [tos.hyp — fonctions de répertoires](https://freemint.github.io/tos.hyp/en/gemdos_directory.html) : lecteurs et répertoires courants.
- **[S9]** [Linux man-pages — `rename(2)`](https://man7.org/linux/man-pages/man2/rename.2.html) : garanties du remplacement Unix utilisées comme point de comparaison.
- **[S10]** [Linux man-pages — `syscalls(2)`](https://man7.org/linux/man-pages/man2/syscalls.2.html) : différence entre interface noyau et fonction de bibliothèque.
- **[S11]** [tos.hyp — codes d’erreur GEMDOS](https://freemint.github.io/tos.hyp/en/gemdos_errors.html) : erreurs historiques et variantes des systèmes étendus.
- **[S12]** [GNU C Library — vérifier les erreurs](https://sourceware.org/glibc/manual/latest/html_node/Checking-for-Errors.html) : sentinelles, `errno` et limites de son interprétation.
- **[S13]** [Linux man-pages — `errno(3)`](https://man7.org/linux/man-pages/man3/errno.3.html) : représentation et état propre au thread sous GNU/Linux.
- **[S14]** [GCC — accès aux objets volatils](https://gcc.gnu.org/onlinedocs/gcc/Volatiles.html) : accès, champs de bits et absence de barrière générale pour les objets non volatils. Ce document décrit GCC ; il n’est pas une spécification des extensions assembleur de stcc.
- **[S15]** [Motorola/NXP — M68000 User’s Manual](https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf) : organisation mémoire, alignement, privilèges et traitement des interruptions, notamment sections 2 et 6.
- **[S16]** [Documentation du noyau Linux — limites de `volatile`](https://docs.kernel.org/process/volatile-considered-harmful.html) : distinction entre accès volatils et primitives de synchronisation. Les primitives de ce noyau ne sont pas fournies par stcc.
- **[S17]** [The Atari Compendium — BIOS](https://bus-error.nokturnal.pl/atari_compendium/html/chap3.htm) : vecteurs, interruptions et réentrance des services système.
