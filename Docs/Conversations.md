# Conversations et compatibilité — 0.4.1

Le service est indépendant de CyUI. Les callbacks s'exécutent sur le worker de l'appel ; l'application
transfère les événements vers sa boucle UI. Utiliser `send_async` pour ne pas bloquer cette boucle.
Ne pas rappeler une méthode verrouillante du fournisseur depuis son callback ; mettre les commandes
dans la file de l'hôte. `Conversation::cancel()` peut être appelé directement, y compris depuis un callback.

## Échecs et annulations

Lorsqu’un fournisseur refuse un tour, `Error::code` identifie la catégorie SDK et
`Error::provider_code` expose le code natif disponible. `what()` contient l’explication
expurgée. L’événement `TurnFailed` transmet les mêmes informations dans `code`,
`provider_code` et `message`. Une annulation déclenche `TurnCancelled`.

`Conversation::transcript()` réunit les messages et les échecs de génération pour l’affichage.
Le checkpoint conserve les diagnostics de ces tours et leurs réponses partielles. L’historique
envoyé au modèle reste accessible avec `history()`. Les checkpoints plus anciens restent lisibles.

## Processus persistants et transports

Le registre utilise par défaut Claude Code en entrée/sortie `stream-json` et Antigravity en entrée
`stream-json`, avec stdin ouvert entre deux tours. `settings.persistent=false` garde le mode par tour.
Codex conserve son App Server. Chaque instance est réservée à une conversation active.
Claude vérifie seulement la disponibilité du runtime lors de `connect`. Le SDK ne demande pas de reconnexion
préalable et conserve les réglages utilisateur natifs (`setting_sources="user"` par défaut). Les catégories
d'échec viennent des réponses structurées du CLI pendant le tour ; elles ne sont pas déduites d'un compte supposé absent.

`opencode` utilise maintenant `opencode acp`, compatible avec le protocole ACP v1 documenté dans
OpenCode actuel (dont 2.x). L'ancien adaptateur JSONL 1.x est nommé `opencode-cli` : changer cet ID
pour conserver le comportement 0.3. Les versions et comptes réels restent à valider individuellement.

| Profil ACP | Commande ajoutée au préfixe configuré |
|---|---|
| `opencode` | `opencode acp` |
| `gemini` | `gemini --acp` |
| `qwen-code` | `qwen --acp` |
| `kimi` | `kimi acp` |
| `goose` | `goose acp` |
| `command-code-acp` | binaire Command Code configuré + `acp` |
| `codebuddy-acp` | `codebuddy --acp` |
| `devin` | `devin` + options natives de droits + `acp` |
| `acp` | commande et `settings.acp_arguments` fournis par l'hôte |

Les profils sont des recettes de lancement du même transport testé ; ils ne constituent pas une preuve
de conformité de chaque version fournisseur. Pour Command Code sous Windows, configurer son véritable
binaire ou un préfixe Node : ne pas utiliser `cmd.exe`, qui est le shell Windows. Pour un ancien Gemini,
`settings.acp_arguments=["--experimental-acp"]` remplace explicitement l'option actuelle.

L'agent annonce les modèles/modes, le chargement des sessions et les capacités de prompt. Le SDK vérifie
ces annonces avant emploi. ACP v2 est encore distinct et refusé ; aucun fallback après génération.
Les modes natifs de l'agent restent applicables. `allow_all_actions=true` autorise les demandes ACP ;
`settings.mode` sélectionne en plus un mode natif annoncé, lorsque l'agent en expose un.

## Services client ACP facultatifs

Par défaut, aucun service de fichiers ou de terminal client n'est annoncé. Un hôte peut les fournir :

```cpp
config.settings["client_capabilities"] = {{"fs", {{"readTextFile", true}}}};
config.approvals = Policy::allow_all(); // ou un callback qui décide par méthode et paramètres
config.request_handler = [](const std::string& method, const Json& args, const CallContext& ctx) {
    ctx.cancellation.check();
    // Résoudre ici le fichier dans l'espace autorisé par l'app.
    return Json{{"content", read_app_document(args.at("path"))}};
};
```

Le callback n'est appelé que pour un service annoncé, la session exacte et une décision positive de la
politique. `terminal=true` expose les requêtes ACP create/output/release/wait_for_exit/kill au même
callback. L'hôte possède leurs implémentations et leur nettoyage ; le SDK ne lance aucun terminal pour
une simple annonce de capacité. Les erreurs retournées à l'agent ne reprennent pas le détail privé des exceptions.

## Pièces jointes portables

```cpp
auto content = Json::array({text_block("Analyse ce document et cette image"),
                           attachment_from_file(pdf_path),
                           attachment_from_file(png_path)});
auto task = chat.send_async(executor, content, context);
```

`attachment_from_file` lit uniquement un fichier sélectionné explicitement par l'hôte et en capture le
contenu. Il reconnaît PNG/JPEG/WebP/GIF, PDF, WAV/MP3, TXT/MD/JSON ; fournir le MIME pour une autre extension.
Limite : 4 Mio par fichier, 8 Mio pour un message/requête. Les pièces jointes sont embarquées dans
l'historique ; les gros fichiers demandent un transport d'upload spécifique à l'hôte/fournisseur.
Aucune URL n'est téléchargée par le SDK et aucun fichier n'est lu à partir d'une demande du modèle.

Blocs canoniques JSON :

```json
[
  {"type":"text","text":"Voici le contexte"},
  {"type":"image","mimeType":"image/png","data":"BASE64"},
  {"type":"audio","mimeType":"audio/wav","data":"BASE64"},
  {"type":"resource","resource":{"uri":"attachment:spec","mimeType":"text/plain","text":"Contenu"}},
  {"type":"resource","resource":{"uri":"attachment:pdf","name":"spec.pdf","mimeType":"application/pdf","blob":"BASE64"}}
]
```

Base64, MIME, taille et structure sont validés. Le bloc `image_url` de Chat Completions reste accepté
pour une URL HTTPS ; utiliser les blocs `image` pour les fichiers locaux. Les blocs inconnus ou non
supportés sont refusés explicitement ; ils ne sont jamais supprimés silencieusement.

| Transport | Entrées traduites |
|---|---|
| OpenAI Responses | Texte, images, ressources texte et fichiers embarqués |
| Anthropic Messages / Claude Code persistant | Texte, images, ressources texte et PDF |
| Ollama | Texte, images base64 natives et ressources texte |
| Compatible Chat Completions / OpenRouter | Texte, images, ressources texte et audio WAV/MP3 |
| Codex App Server | Texte, images sous forme d'URL de données et ressources texte |
| ACP v1 | Texte ; images/audio/ressources si l'agent annonce la capacité correspondante |
| Antigravity et CLI par tour | Texte et tableaux de blocs texte |

`image_input`, `audio_input`, `file_input` et `persistent_process` complètent `capabilities()`.
Une capacité de transport ne garantit pas que le modèle choisi l'accepte. Vidéo, conversion de médias,
upload serveur de gros fichiers et génération audio/vidéo dédiée ne sont pas inclus.

## Sauvegarder, restaurer et reconnecter

```cpp
ConversationStore store(app_private_history_directory); // module facultatif, lié à CyMCP::AI
store.save("document-42", chat);

Conversation restored(providers.create("openai", config), exact_model_id);
restored.restore(store.load("document-42"), context);
restored.send_async(executor, "Continue", context);

chat.disconnect();
chat.reconnect(context);
```

Le checkpoint JSON versionné contient l'ID du fournisseur, le mode d'historique, l'ID de session,
les modèles, les messages, les données natives nécessaires aux tours suivants et le dernier échec.
Il n'inclut pas la configuration ni les clés du fournisseur. Il reste constitué de données privées :
prompts, réponses, fichiers et raisonnement opaque peuvent être présents. L'hôte choisit la conservation
et ajoute son chiffrement si nécessaire ; `checkpoint()` permet aussi d'utiliser sa propre base de données.

Le stockage fourni protège le répertoire, borne les lectures, refuse les liens et IDs de chemins,
puis remplace atomiquement le fichier JSON. Les écritures concurrentes sur le même ID suivent le dernier
remplacement réussi ; l'hôte gère son éventuelle synchronisation. Ce n'est pas une base transactionnelle.

`restore` vérifie version, fournisseur, mode, modèle et messages avant connexion. Pour les agents natifs,
il exige la reprise de l'ID exact et refuse une nouvelle session de substitution. Les API récupèrent
l'historique applicatif et conservent l'identité locale. Une restauration ne lance aucune génération.
L'ID identifie le profil de fournisseur ; l'hôte doit réutiliser son compte, endpoint et configuration associés.

Après annulation ou erreur, les résultats d'outils déjà exécutés restent conservés. `reconnect` ne renvoie
pas le message interrompu. L'application consulte `status()` et propose une nouvelle instruction explicite.
Un état distant peut avoir changé avant une coupure : aucune garantie d'annulation rétroactive n'est annoncée.
Les checkpoints pendant une opération active ou planifiée sont refusés avec `Busy`.

## Mesurer la fluidité

Chaque `ChatResult.metrics` et l'événement `TurnMetrics` donnent `first_token_ms`, `total_ms` et le nombre
d'événements. Le premier token vaut `null` si aucun delta texte n'a été observé. Ces temps incluent le
transport et les callbacks ; ils ne constituent pas un benchmark matériel du modèle.

```sh
cymcp_chat benchmark CONFIG.json
```

Le benchmark effectue trois petits échanges successifs avec le fournisseur configuré (1 à 20 via
`benchmark_turns`). Il rapporte connexion, tour froid, tours chauds, processus lancés et résultat attendu.
Il utilise le compte ou la clé configurés et peut donc consommer son quota. Aucun essai n'est relancé en erreur.

Le banc apparié Ollama compare le premier texte visible à la sortie de `cymcp_chat` avec le
premier texte de la réponse NDJSON de l’API native locale. Un même prompt synthétique, un
modèle déjà installé et les mêmes réglages servent aux deux chemins. Chaque mesure démarre
une conversation neuve. Le chronométrage commence à l’envoi du message, après le démarrage
du processus client. Un échauffement par chemin précède six paires, exécutées en ordre alterné.

```sh
python benchmarks/chat_latency.py build/cymcp_chat --model qwen3:0.6b
```

Le 18 novembre 2026, sous Windows x64 avec Ollama 0.40.5, Qwen3 0.6b et CUDA, les douze
réponses mesurées ont donné le texte attendu. La réflexion était désactivée et le plafond
de sortie était de 96 tokens.

| Médiane sur six mesures par chemin | Premier texte visible | Réponse terminée |
| --- | ---: | ---: |
| SDK C++ via `cymcp_chat` | 15,0 ms | 18,1 ms |
| Client HTTP direct sur `/api/chat` | 13,4 ms | 16,5 ms |

Ces valeurs caractérisent un échange court sur la machine testée, avec le cache du modèle
chaud et le même préfixe à chaque requête. Le rendu graphique, le chargement à froid, les
longs historiques et les fournisseurs distants demandent des mesures distinctes.

L'exemple de tchat accepte aussi `{"content":[...]}` ou `{"text":"Analyse", "attachments":[{"path":"..."}]}`.
Configuration de stockage facultative : `"history":{"directory":"...","id":"main","restore":false}`.
Mettre `restore=true` pour charger un checkpoint existant. Chaque tour terminé ou en échec est sauvegardé.
`{"command":"reconnect"}`, `{"command":"disconnect"}` et `{"command":"save"}` sont des commandes locales.

## Références vérifiées le 14 novembre 2026

[OpenCode ACP](https://opencode.ai/docs/cli/acp/),
[Claude streaming input](https://code.claude.com/docs/en/agent-sdk/streaming-vs-single-mode),
[Antigravity stdin persistant](https://antigravity.google/docs/cli/headless/),
[ACP content](https://agentclientprotocol.com/protocol/v1/content),
[Gemini ACP](https://github.com/google-gemini/gemini-cli/blob/main/docs/cli/acp-mode.md),
[Qwen configuration](https://github.com/QwenLM/qwen-code/blob/main/docs/users/configuration/settings.md),
[Kimi commandes](https://github.com/MoonshotAI/kimi-cli/blob/main/docs/en/reference/kimi-command.md),
[goose ACP](https://github.com/aaif-goose/goose/blob/main/documentation/docs/gdk/acp/index.md),
[Command Code ACP](https://commandcode.ai/docs/acp),
[CodeBuddy ACP](https://www.codebuddy.ai/docs/cli/acp),
[OpenAI fichiers](https://developers.openai.com/api/docs/guides/file-inputs),
[Ollama vision](https://docs.ollama.com/capabilities/vision).
Les preuves locales et les essais réels en échec sont distingués dans [Validation.md](Validation.md).
