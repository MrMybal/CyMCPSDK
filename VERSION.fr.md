# Notes de version de CyMCPSDK

[English](VERSION.md) · [Manuel](Documentation/Documentation.html#/fr/overview)

## 0.5.2 — En développement

- L'anglais reste la langue par défaut de l'application. La démonstration Markdown locale
  suit maintenant elle aussi la langue sélectionnée, anglais ou français.
- Chat Lab propose une action **Aide (F1)** qui ouvre le manuel local dans la langue sélectionnée.
- Un manuel CyDoc bilingue accompagne l'application et s'ouvre directement depuis le disque.
- Le README suit la même structure en anglais et en français, avec les modules, la compilation et les licences.

### Problèmes connus

- La compilation Linux rencontre actuellement une erreur GCC dans une déclaration du test du cœur.
- Sous macOS, le test de stockage de conversation échoue lorsque le dossier temporaire a un ancêtre
  sous forme de lien symbolique. Aucune distribution complète Linux/macOS n'est encore proposée.
  L'exemple graphique utilise Win32/DX11.
- La disponibilité d'un fournisseur dépend de la version du CLI/API, du compte et du modèle choisi.
  La présence d'un profil ne garantit pas l'authentification auprès de chaque service.

### Limites actuelles

- Le SDK n'inclut ni modèles, CLI fournisseurs, moteurs d'inférence, runtimes GPU ou pilotes.
- Les téléchargements locaux vérifient des manifestes fournis par l'hôte ; l'extraction automatique
  d'archives, les catalogues de paquets GPU et la reprise HTTP Range restent à implémenter.
- L'attachement direct des opérations dans Chat Lab est disponible pour Codex et les transports API
  compatibles ; les autres CLI utilisent leurs réglages MCP natifs. Les services ACP fichiers/terminal
  nécessitent des callbacks de l'hôte.
- Les outils MCP passent par stdio ou HTTP JSON loopback authentifié. Les souscriptions persistantes,
  ressources/prompts et notifications de progression ne sont pas annoncés.
- Android est reporté. La compatibilité source C++ évolue ; aucun ABI entre compilateurs n'est garanti.

Signaler les problèmes reproductibles dans le [suivi des problèmes](https://github.com/MrMybal/CyMCPSDK/issues).
Aucune release binaire publique n'a été publiée pour cette version ; compiler depuis les sources.

## Versions précédentes

- **0.5.1 :** commandes bidirectionnelles de Chat Lab par MCP/SDK, correction du streaming HTTP Windows,
  du budget d'affichage de la réflexion et conservation des durées des tours échoués.
- **0.5.0 :** module de tchat CyUI optionnel et exemple Windows Chat Lab.
- **0.4.1 :** réglages natifs de Claude conservés et suppression du contrôle de login bloquant.
- **0.4.0 :** profils fournisseurs extensibles, conversations, pièces jointes, historique privé et reprise exacte.
- **0.3.0 :** socle natif modulaire du SDK.
