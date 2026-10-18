# Dix-neuf étapes pédagogiques

Les étapes 1 à 14 et 16 à 19 fonctionnent sous TOS. L'étape 15 produit une ROM
autonome ; les étapes 16, 17 et 19 fonctionnent également dans une ROM stcc.

Depuis la racine : `make examples`. Les exécutables ont des noms 8.3 dans `bin/`.
Aucun exemple n'utilise une bibliothèque Atari externe.

| Étape | Source | Ce que l'on observe |
|---|---|---|
| 1 | `01_minimal.c` | retour de 42, transmis à Pterm |
| 2 | `02_calculs.c` | boucle, conversions, somme de 1 à 100 |
| 3 | `03_structures.c` | tableau de structures, pointeur, accès `->` |
| 4 | `04_recursion.c` | cadres de pile et factorielle sur long |
| 5 | `05_gemdos.c` | chaîne C, paramètre inline et TRAP #1 |
| 6 | `06_clavier.c` | BIOS Bconin ; attend une touche et retourne son ASCII |
| 7 | `07_fichier.c` | création, écriture, fermeture de `STCC.TXT` |
| 8 | `08_video.c` | Physbase, accès volatile, motif temporaire puis restauration |
| 9 | `09_multi/` | deux unités C, en-tête et variable externe |
| 10 | `10_mix/` | routine Devpac respectant l'ABI appelée depuis C |
| 11 | `11_bits.c` | représentation big-endian et mises à jour des champs de bits |
| 12 | `12_interrupts.c` | exceptions imbriquées, retour RTE et restauration du contexte |
| 13 | `13_naked_chain.c` | chaînage avec une entrée assembleur brute |
| 14 | `14_vbl.c` | callback de la file VBL TOS avec retour RTS |
| 15 | `15_rom/main.c` | ROM autonome : accès vidéo direct sans TOS (`make rom`) |
| 16 | `16_registers.c` | placement automatique, D3/A3 imposés et modification inline |
| 17 | `17_stack.c` | fonction C sur une pile secondaire de 32 Kio, puis retour à l'appelant |
| 18 | `18_args.c` | utilitaire TTP affichant les arguments, citations et chemins |
| 19 | `19_portable.c` | macros de cible, entiers longs et source commune ST/PC |

L'étape 8 fonctionne quelle que soit la résolution ST : elle ne touche que
les 640 premiers octets de l'écran. Le motif est très bref ; augmenter la
boucle d'attente pour l'observer plus longtemps. Il ne change pas de mode vidéo.

L'étape 15 se construit séparément par `make rom` et donne `bin/15ROM.IMG`
avec sa carte de symboles. C'est une image ROM pour remplacer le système au
démarrage, pas un programme à lancer depuis GEM. Elle affiche quatre lignes
en couleur PAL ou en monochrome, puis conserve l'écran avec le CPU arrêté.
Voir [ROM.md](../docs/ROM.md) pour la commande Hatari et les hypothèses ST/512 Kio.

L'étape 17 utilise un trampoline `__naked__` avec une sauvegarde unique de SP.
Elle n'est pas réentrante. Voir [REGISTERS.md](../docs/REGISTERS.md) pour les
registres disponibles, le choix de la pile au démarrage et les contrats d'appel.

L'étape 18 se lance comme `bin/18ARGS.TTP` depuis le bureau ou un shell.
Les arguments sont affichés entre crochets ; sans argument, elle ne produit
aucune ligne. Le nom du programme n'est connu que si le parent utilise ARGV.
Voir [ARGUMENTS.md](../docs/ARGUMENTS.md).

L'étape 19 se compile aussi avec `cc -std=c89 examples/19_portable.c` et retourne
42 sur les deux cibles. Voir [PORTABILITY.md](../docs/PORTABILITY.md).

Pour inspecter la traduction :

```sh
./stcc -S examples/04_recursion.c -o factorielle.s
./stcc examples/10_mix/main.c examples/10_mix/twice.s -o MIX.TOS --map MIX.map
```

Pour une expérience sans démarrage C, assembler un module contenant sa propre
entrée et son propre appel de terminaison avec `--nostartfiles`. Sinon, le runtime
attend une fonction `_main`, c'est-à-dire une définition C de `main`.

Les élèves peuvent partir des bindings de l'étape 7 pour en écrire d'autres.
L'ordre des paramètres GEMDOS est celui de TOS, indépendant de l'ordre des
arguments C ; le bloc inline construit explicitement la pile système.

Pour mettre tous les exécutables sur une disquette :

```sh
make all examples
./stmkdisk -o COURS.ST --root examples/bin
```

Insérer `COURS.ST` dans A sous Hatari ou Steem Engine et ouvrir le lecteur
sur le bureau. Pour un seul programme lancé au démarrage :

```sh
./stmkdisk -o DEMO.ST --auto examples/bin/05GEMDOS.TOS
```

Voir `docs/MKDISK.md` pour les ressources, les noms 8.3 et les capacités.
