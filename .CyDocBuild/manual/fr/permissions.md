---
id: permissions
title: Configurer les droits
description: Choisir séparément la politique des opérations hôtes et les autorisations de l'agent natif.
section: SDK
parent: integration
order: 2
sourceRevision: 1
---

## Choisir la politique hôte {#policy}

Une `Policy` par défaut refuse les opérations. Utiliser un callback pour décider à partir du
contexte, du nom de l'opération et des arguments, ou choisir explicitement `Policy::allow_all()`
lorsque l'hôte veut un accès libre à son domaine enregistré. Le SDK n'impose pas de boîtes de confirmation.

```cpp
cymcp::Policy reads_only([](const auto&, const std::string& name, const auto&) {
    return name == "get_counter" ? cymcp::Decision::allow : cymcp::Decision::deny;
});
```

Autoriser une opération ne crée pas de capacités supplémentaires. Limiter les handlers et leurs
schémas au besoin de l'application. La validation du domaine s'applique même avec une politique
permissive. L'annulation et les budgets de ressources sont distincts des décisions de droits.

## Autorisations de l'agent et opérations {#agents}

Les droits natifs de l'agent sont distincts du registre d'opérations. Dans Chat Lab,
**Autoriser toutes les actions** demande le mode permissif du fournisseur ; **Autoriser la
modification du compteur** contrôle le registre de démonstration. Une option activée ne retire
pas les restrictions imposées par un fournisseur externe ou le système d'exploitation.

Les services client ACP de fichiers et terminal ne sont pas annoncés par défaut. L'hôte doit
déclarer `settings.client_capabilities`, fournir `request_handler` et choisir sa politique
d'autorisation. Il implémente le comportement et le nettoyage des fichiers/terminaux ; une simple
capacité déclarée ne crée aucun service de terminal.

## Authentifier un serveur local {#network}

Le serveur HTTP loopback exige un jeton d'au moins 32 caractères et refuse les requêtes portant
`Origin`. Passer le jeton dans `Authorization: Bearer ...`. Chat Lab crée un nouveau jeton aléatoire
au démarrage du serveur ; il affiche uniquement l'adresse et propose une copie explicite du jeton.

L'authentification identifie un client autorisé ; elle ne remplace pas les droits des opérations.
La durée de vie du serveur, la distribution du jeton et la politique du domaine restent contrôlées par l'hôte.
