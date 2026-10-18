# État de stcc 0.6

## Implémenté

- Construction native Linux en C99, sans générateur de parseur ni code repris
  d'un autre compilateur. Dépendance d'exécution côté hôte : libc Linux.
- Chaîne autonome C → AST typé → Devpac → STCO → TOS, sans outil croisé.
- Entiers ILP16/32, conversions, promotions, arithmétique 32 bits logicielle.
- Tableaux multidimensionnels, structures, unions, enums et typedef.
- Champs de bits C89 int/unsigned int de 1 à 16 bits, padding anonyme et largeur
  zéro, initialisations, promotions, lectures/écritures et opérateurs composés.
- Pointeurs, fonctions, récursion, pointeurs de fonctions et arguments variadiques.
- Variables globales, locales, `static`, `extern`, `const`, `volatile`, `register`.
- Placement effectif des scalaires locaux/paramètres `register`, repli mémoire
  si nécessaire ; `register(d3)` / `register(a3)` imposent un registre non volatil.
  Sauvegarde aux appels, accès inline et restrictions : voir REGISTERS.md.
- Affectations et opérateurs composés, incréments, logique court-circuit,
  opérateur conditionnel, virgule, `sizeof`, accès aux membres.
- `if/else`, `while`, `do`, `for`, `switch/case/default`, `break`, `continue`,
  `goto` et labels de fonction.
- Initialisations statiques et automatiques, chaînes, agrégats avec accolades,
  remplissage à zéro des éléments omis, affectation/copie de structures.
- Prototypes, déclarations implicites de fonction et définitions de fonctions
  à retour `int` implicite ; définitions avec paramètres prototypés.
- Préprocesseur : includes C, macros objet/fonction, `#`, `##`, `#if/#elif`,
  `defined`, `#ifdef/#ifndef/#else/#endif`, `#undef`, `#error`, `-I`, `-D`.
  Les expressions `#if` emploient les longs cibles et le court-circuit.
- Macros prédéfinies du compilateur, de la cible ST/68000, de version, des
  tailles des types et de l'ordre des octets ; voir PORTABILITY.md.
- En-têtes cibles `limits.h`, `stddef.h`, `stdarg.h`, `stcc.h`.
- Assembleur 68000 Devpac, objets persistants, symboles locaux/externes,
  fixups, liaison multifichier et relocations TOS.
- Inline Devpac avec accès aux variables C et préservation des non-volatils.
- `__interrupt__` : sauvegarde intégrale et retour RTE ; `__naked__` : entrée
  entièrement assembleur sans instructions ajoutées. Voir INTERRUPTS.md.
- Runtime TOS : basepage, BSS, pile, Mshrink, main(void) ou main(int, char **),
  ligne courte et extension ARGV, Pterm ; voir ARGUMENTS.md.
- Pile de démarrage configurable avec `--stack-size` ou `--stack-top`, en PRG
  et en ROM ; exemple de changement de pile via un trampoline `__naked__`.
- Mode ROM : liaison absolue, code en ROM et variables en RAM, copie DATA,
  effacement BSS, pile, vecteurs, démarrage ST 512 Kio autonome ; voir ROM.md.
- `stmkdisk` : images de disquette FAT12 `.ST` de 360/720/800 Kio, import
  récursif, chemins 8.3, dossier AUTO ; voir MKDISK.md.
- Exemples et infrastructure de tests CPU et Hatari, fichiers Debian.

## Non implémenté ou incomplet — ne pas annoncer ANSI C89 complet

| Partie | Limite actuelle |
|---|---|
| Flottants | `float`, `double`, `long double`, constantes et support logiciel absents ; rejet explicite |
| Records | paramètres et résultats d'agrégat par valeur volontairement exclus ; champs de bits limités aux types int C89 |
| Initialisation | pas d'élision des accolades des agrégats imbriqués ; accolades explicites nécessaires |
| Littéraux larges | constantes `L'x'` et chaînes `L"..."` non prises en charge |
| Fonctions | définitions K&R à liste d'identifiants et déclarations séparées non prises en charge |
| Déclarations | pas de fusion des définitions provisoires identiques entre objets (« common ») ; utiliser `extern` dans les autres unités |
| Préprocesseur | pas de conformité complète du rescanning/hide-set dans les cas complexes ; pas de trigraphes, `#line` utilisateur, `#pragma`, `__DATE__` ou `__TIME__` |
| Diagnostics | fichier et ligne C conservés par marqueurs ; après continuation de ligne les numéros peuvent être décalés ; erreurs de l'assembleur sur l'émission C indiquées dans l'assembleur généré |
| Inline | substitution de variables simples ; pas de membre C, sortie anticipée C, ou analyse prouvant les effets arbitraires de l'assembleur |
| ABI | scalaires/pointeurs seulement pour passage/retour ; pas de garantie binaire avec GCC/Pure C/Lattice |
| Registres | liaison locale à D2–D7/A2–A5 seulement ; pas de variable globale en registre ni de changement d'ABI ; clobbers des appels assembleur à gérer |
| Objets | STCO interne seulement ; pas de lecteur DRI/GST/ELF ni d'archives `.a` |
| Débogage | carte de symboles texte ; pas encore de table de symboles DRI dans le PRG |
| Démarrage | pas de troisième paramètre envp ni de desk accessory ; ARGV historique seulement, sans décodage de l'extension NULL pour arguments vides |
| ROM | profil ST fixe de 512 Kio sans détection RAM ; pas d'API TOS ni de section C rodata distincte ; validation Hatari seulement |
| Ressources | pile de 16 Kio par défaut, configurable sans garde de débordement ; cadre local 32760 octets maximum, branches 16 bits sans relaxation |
| Bibliothèque | aucune libc Atari ; seulement les en-têtes et primitives du compilateur |
| Assembleur | voir DEVPAC.md pour les directives et extensions manquantes |

C89 n'a pas de `//`, `long long`, déclarations après une instruction ou
variables déclarées dans l'en-tête d'un `for` : ces syntaxes sont rejetées.
Le système de diagnostics n'est pas un vérificateur complet de toutes les
contraintes de la norme ; les cas limites du langage restent à élargir.

Les tests positifs valident des programmes complets et ne servent pas de liste
spéciale de programmes reconnus : l'analyse et la génération sont générales.
Cela ne constitue cependant ni une certification ANSI, ni une preuve exhaustive.

## Suite proposée

1. Étendre les tests différenciés aux coins du langage et à la matrice 68000.
2. Compléter le préprocesseur et les initialisations C89 sans accolades explicites.
3. Conserver les flottants logiciels et la virgule fixe comme sujets d'exercices,
   sans en faire une dépendance du compilateur ; les agrégats passent par adresse.
4. Ajouter relaxation de branches, symboles de débogage et objets historiques.
5. Ajouter une IR linéaire lisible pour l'enseignement, puis de petites optimisations.

Les tests sur Hatari/EmuTOS ne remplacent pas une vérification sur un Atari ST
physique et sur les versions propriétaires de TOS que vous souhaitez utiliser.
