# Quatorze étapes pédagogiques

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

L'étape 8 fonctionne quelle que soit la résolution ST : elle ne touche que
les 640 premiers octets de l'écran. Le motif est très bref ; augmenter la
boucle d'attente pour l'observer plus longtemps. Il ne change pas de mode vidéo.

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
