# Fabriquer une disquette pour Hatari et Steem Engine

`stmkdisk` est un **binaire Linux**, construit par `make` et installé avec
`stcc`. Il produit une image de disquette `.ST` brute avec un système de fichiers
FAT12. Il écrit lui-même le secteur de paramètres, les deux FAT, les répertoires
et les données. Aucun montage, privilège root, `mkfs` ou `mtools` n'est nécessaire.

## Utilisation

```sh
make
./stcc examples/05_gemdos.c -o BONJOUR.TOS
./stmkdisk -o DEMO.ST BONJOUR.TOS
```

Insérer `DEMO.ST` dans le lecteur A de l'émulateur. Sur le bureau Atari,
ouvrir A puis lancer `BONJOUR.TOS`.

Sous Hatari, avec une ROM TOS ou EmuTOS que vous possédez :

```sh
hatari --tos /chemin/etos192us.img --disk-a DEMO.ST
```

Dans Steem Engine, insérer la même image `.ST` dans A depuis le gestionnaire
de disquettes. Les deux émulateurs attendent une ROM séparée de la disquette.

## Fichiers, dossiers et noms Atari

```sh
./stmkdisk -o JEU.ST JEU.PRG DATA SON
```

Les fichiers apparaissent à la racine et les dossiers sont copiés récursivement
avec leur nom : ici `A:\JEU.PRG`, `A:\DATA\...` et `A:\SON\...`.
Les dossiers vides sont conservés. Les liens symboliques et fichiers spéciaux
sont rejetés ; aucune conversion des octets des fichiers n'est faite.

Pour copier seulement le **contenu** d'un dossier à la racine :

```sh
./stmkdisk -o COURS.ST --root examples/bin
```

Les noms dans l'image sont en majuscules ASCII, au format **8.3** : au plus
huit caractères avant le point et trois après. Il n'y a ni VFAT ni troncature
automatique. Les collisions après passage en majuscules sont rejetées.
Les noms longs côté Linux peuvent être renommés explicitement :

```sh
./stmkdisk -o JEU.ST JEU.PRG \
    --add documentation-du-jeu.txt LIREMOI.TXT \
    --add ressources/texture-principale.bin DATA/TEXTURE.BIN
```

`--add SOURCE DEST` accepte un fichier ou dossier source. `DEST` est un chemin
relatif dans l'image, avec `/` comme séparateur et des composants 8.3. Les
dossiers parents nécessaires sont créés. Des imports dans un même dossier se
fusionnent ; deux fichiers de même nom provoquent une erreur. Les chemins
absolus et composants `.`/`..` ne sont pas acceptés comme destinations.

## Lancement automatique par TOS

```sh
./stmkdisk -o DEMO.ST --auto BONJOUR.TOS
```

Cette commande crée `A:\AUTO\BONJOUR.PRG`. Le fichier source `.TOS` est copié
tel quel, sous l'extension `.PRG` que TOS utilise pour les programmes AUTO.
Un fichier source `.PRG` garde son nom. Les deux extensions partagent le format
exécutable TOS ; cela ne transforme pas le programme en application GEM.

TOS lance les programmes AUTO pendant son démarrage. Réinitialiser l'émulateur
avec la disquette dans A pour les lancer. Cette option convient aux programmes
qui peuvent s'exécuter à ce stade ; une application qui attend le bureau AES
doit être lancée depuis le bureau. Plusieurs `--auto` sont possibles, dans
l'ordre des noms courts triés dans l'image.

## Capacités et options

| Option | Effet |
|---|---|
| `-o IMAGE.ST` | sortie obligatoire |
| `--size 360` | 360 Kio, 80 pistes, une face, 9 secteurs/piste |
| `--size 720` | **défaut** : 720 Kio, 80 pistes, deux faces, 9 secteurs/piste |
| `--size 800` | 800 Kio, 80 pistes, deux faces, 10 secteurs/piste |
| `--label NOM` | label de volume ASCII de 1 à 11 caractères ; défaut `STCC` |
| `--root DOSSIER` | importer le contenu à la racine |
| `--add SOURCE DEST` | choisir le chemin de destination Atari |
| `--auto PROGRAMME` | copier un `.PRG`/`.TOS` dans `AUTO` avec l'extension `.PRG` |
| `--force` | remplacer explicitement une image existante |
| `--` | les arguments suivants sont des chemins source |
| `--help`, `--version` | aide et version |

Sans fichier source, l'outil crée une disquette formatée vide. La place utile
est inférieure à la capacité brute : FAT, catalogue et répertoires consomment
de l'espace. Le catalogue racine comporte 112 entrées, dont celle du label :
111 fichiers ou dossiers au maximum à la racine. Les sous-dossiers peuvent
occuper plusieurs clusters. La profondeur est limitée à 64 niveaux.

Le format 360 Kio est celui d'un ST simple face à 80 pistes ; ce n'est pas la
géométrie PC 5¼ pouces à 40 pistes et deux faces. Le format 800 Kio utilise
dix secteurs par piste ; 720 Kio constitue le choix standard pour commencer.

L'outil prépare l'image en mémoire puis publie un fichier temporaire dans le
répertoire de sortie. Une erreur de nom, de capacité ou de lecture laisse une
image existante intacte, même avec `--force`. Il refuse aussi d'utiliser un
fichier d'entrée comme fichier de sortie. La publication sans écrasement utilise
un lien physique POSIX ; le répertoire de sortie doit les prendre en charge.

Les entrées sont triées, les dates FAT fixées au 1er janvier 1980 et le numéro
de série dérivé du contenu : les mêmes fichiers, noms et options produisent
les mêmes octets. Une modification de contenu change normalement le numéro
de série sur 24 bits employé par Atari pour identifier un média.

Le secteur de paramètres est volontairement **non exécutable** : sa somme de
contrôle 68000 ne vaut pas `$1234`. Le démarrage automatique passe par TOS et
le dossier AUTO. Ce n'est ni une image de disque dur ni un format de protection
de disquette STX/IPF.

## Vérifications

```sh
make test
python3 tests/mkdisk_hatari.py --tos /chemin/etos192us.img
```

`tests/mkdisk.py` relit les chaînes FAT indépendamment du générateur, vérifie
les octets copiés, les sous-dossiers, les cas limites et les erreurs.
Le test Hatari démarre sur le lecteur A, sans disque GEMDOS issu d'un dossier
Linux : son programme AUTO lit un fichier de plusieurs clusters dans un
sous-dossier, résout `..` et crée un fichier dont le contenu est ensuite relu
directement dans l'image modifiée. Les trois géométries sont testées.

On peut ajouter `--fsck /chemin/fsck.fat --mtools-dir /chemin/bin` au test
Hatari pour des contrôles externes facultatifs. Ils ne sont pas des dépendances
du binaire. Steem Engine n'a pas été exécuté dans ce banc ; la compatibilité
visée repose sur le format `.ST` brut standard qu'il prend en charge.
