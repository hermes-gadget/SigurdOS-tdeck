// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// The table/fallback approach is adapted from the GPL-3.0-or-later wadamesh
// src/ui-touch/i18n.cpp implementation, with attribution retained in the
// public framework header and docs/I18N.md.

#include "i18n.h"

#include "../hal/prefs.h"
#include "../utils/utf8_util.h"

namespace sigurdos::i18n {
namespace {

constexpr const char* kMissingTranslation = "[missing translation]";

constexpr const char* kLanguageNames[language_count] = {
    "English", "Deutsch", "Français", "Español", "Nederlands", "Italiano",
    "Português",
};

// Keep the English table complete. Other tables may contain nullptr for a
// newly-added string; tr_for() deliberately falls back to this table.
constexpr const char* kEnglish[string_count] = {
    "CHATS",
    "DMs",
    "ROOMS",
    "CONTACTS",
    "REPEATERS",
    "ADVERTISE",
    "MAP",
    "TERMINAL",
    "PACKETS",
    "SETTINGS",
    "SETUP",
    "SIGNAL",
    "Do setup for radio",

    "Language",

    "PUBLIC",
    "No messages yet",
    "Delete channel?",
    "Delete channel #%s?",
    "Cancel",
    "Delete",
    "Channel removal was not saved",
    "Chat",
    "Add # Channel",
    "Search messages...",
    "Load older messages",
    "Return toward newest",
    "No matching messages",
    "Emoji",
    "Close",
    " [FAILED]",
    "Message",
    "Send",
    "Add Channel",
    "Name:",
    "e.g. #general",
    "PSK (optional):",
    "base64 key (blank = public)",
    "Add",
    "Enter channel name",
    "Invalid, full, or save failed",
    "Marked all read",
    "Leave failed",
    "Named: %s (random key)",
    "Named: none (public)",
    "This chat: %s",
    "This chat: none",
    "Invalid scope",
    "Scope was not saved",
    "Clear failed; scope stays active",
    "Named routing scope",
    "local name (random key)",
    "Set",
    "Clear",
    "Cannot open DM: name too long",
    "Cannot open DM: conversation list full",

    "Reserved English fallback",
};

constexpr const char* kGerman[string_count] = {
    "CHATS",
    "DMs",
    "RÄUME",
    "KONTAKTE",
    "RELAIS",
    "ANKÜNDIGEN",
    "KARTE",
    "TERMINAL",
    "PAKETE",
    "EINSTELLUNGEN",
    "EINRICHTUNG",
    "SIGNAL",
    "Funk einrichten",

    "Sprache",

    "ÖFFENTLICH",
    "Noch keine Nachrichten",
    "Kanal löschen?",
    "Kanal #%s löschen?",
    "Abbrechen",
    "Löschen",
    "Kanalentfernung wurde nicht gespeichert",
    "Chat",
    "#-Kanal hinzufügen",
    "Nachrichten suchen...",
    "Ältere Nachrichten laden",
    "Zu den neuesten zurück",
    "Keine passenden Nachrichten",
    "Emoji",
    "Schließen",
    " [FEHLER]",
    "Nachricht",
    "Senden",
    "Kanal hinzufügen",
    "Name:",
    "z. B. #general",
    "PSK (optional):",
    "Base64-Schlüssel (leer = öffentlich)",
    "Hinzufügen",
    "Kanalnamen eingeben",
    "Ungültig, voll oder Speichern fehlgeschlagen",
    "Alle als gelesen markiert",
    "Verlassen fehlgeschlagen",
    "Benannt: %s (zufälliger Schlüssel)",
    "Benannt: keiner (öffentlich)",
    "Dieser Chat: %s",
    "Dieser Chat: keiner",
    "Ungültiger Bereich",
    "Bereich wurde nicht gespeichert",
    "Löschen fehlgeschlagen; Bereich bleibt aktiv",
    "Benannter Routing-Bereich",
    "lokaler Name (zufälliger Schlüssel)",
    "Setzen",
    "Löschen",
    "DM kann nicht geöffnet werden: Name zu lang",
    "DM kann nicht geöffnet werden: Gesprächsliste voll",

    nullptr,
};

constexpr const char* kFrench[string_count] = {
    "CHATS",
    "MP",
    "SALONS",
    "CONTACTS",
    "RELAIS",
    "ANNONCER",
    "CARTE",
    "TERMINAL",
    "PAQUETS",
    "RÉGLAGES",
    "CONFIGURATION",
    "SIGNAL",
    "Régler radio",

    "Langue",

    "PUBLIC",
    "Aucun message",
    "Supprimer le canal ?",
    "Supprimer le canal #%s ?",
    "Annuler",
    "Supprimer",
    "La suppression du canal n'a pas été enregistrée",
    "Chat",
    "Ajouter un canal #",
    "Rechercher des messages...",
    "Charger les anciens messages",
    "Revenir aux plus récents",
    "Aucun message correspondant",
    "Emoji",
    "Fermer",
    " [ÉCHEC]",
    "Message",
    "Envoyer",
    "Ajouter un canal",
    "Nom :",
    "ex. #general",
    "PSK (facultative) :",
    "clé base64 (vide = public)",
    "Ajouter",
    "Saisissez le nom du canal",
    "Invalide, complet ou échec de l'enregistrement",
    "Tout marqué comme lu",
    "Échec de la sortie",
    "Nom : %s (clé aléatoire)",
    "Nom : aucun (public)",
    "Cette discussion : %s",
    "Cette discussion : aucune",
    "Portée invalide",
    "La portée n'a pas été enregistrée",
    "Échec de l'effacement ; la portée reste active",
    "Portée de routage nommée",
    "nom local (clé aléatoire)",
    "Définir",
    "Effacer",
    "Impossible d'ouvrir le MP : nom trop long",
    "Impossible d'ouvrir le MP : liste des conversations pleine",

    nullptr,
};

constexpr const char* kSpanish[string_count] = {
    "CHATS",
    "DMs",
    "SALAS",
    "CONTACTOS",
    "REPETIDORES",
    "ANUNCIAR",
    "MAPA",
    "TERMINAL",
    "PAQUETES",
    "AJUSTES",
    "CONFIGURAR",
    "SEÑAL",
    "Configura radio",

    "Idioma",

    "PÚBLICO",
    "Aún no hay mensajes",
    "¿Eliminar canal?",
    "¿Eliminar el canal #%s?",
    "Cancelar",
    "Eliminar",
    "No se guardó la eliminación del canal",
    "Chat",
    "Añadir canal #",
    "Buscar mensajes...",
    "Cargar mensajes antiguos",
    "Volver a los más recientes",
    "No hay mensajes coincidentes",
    "Emoji",
    "Cerrar",
    " [FALLO]",
    "Mensaje",
    "Enviar",
    "Añadir canal",
    "Nombre:",
    "p. ej. #general",
    "PSK (opcional):",
    "clave base64 (en blanco = pública)",
    "Añadir",
    "Introduce el nombre del canal",
    "Inválido, lleno o error al guardar",
    "Todo marcado como leído",
    "No se pudo salir",
    "Nombre: %s (clave aleatoria)",
    "Nombre: ninguno (público)",
    "Este chat: %s",
    "Este chat: ninguno",
    "Ámbito no válido",
    "No se guardó el ámbito",
    "No se pudo borrar; el ámbito sigue activo",
    "Ámbito de enrutamiento con nombre",
    "nombre local (clave aleatoria)",
    "Establecer",
    "Borrar",
    "No se puede abrir el DM: nombre demasiado largo",
    "No se puede abrir el DM: lista de conversaciones llena",

    nullptr,
};

constexpr const char* kDutch[string_count] = {
    // Home tiles (uppercase, 8px floor via the text-fit ladder)
    "CHATS",
    "DMs",
    "KAMERS",
    "CONTACTEN",
    "REPEATERS",
    "AANKONDIGEN",
    "KAART",
    "TERMINAL",
    "PAKKETTEN",
    "INSTELLINGEN",
    "INSTALLATIE",
    "SIGNAAL",
    "Radio instellen",

    "Taal",

    "OPENBAAR",
    "Nog geen berichten",
    "Kanaal verwijderen?",
    "Kanaal #%s verwijderen?",
    "Annuleren",
    "Verwijderen",
    "Kanaalverwijdering is niet opgeslagen",
    "Chat",
    "#-kanaal toevoegen",
    "Berichten zoeken...",
    "Oudere berichten laden",
    "Terug naar nieuwste",
    "Geen overeenkomende berichten",
    "Emoji",
    "Sluiten",
    " [MISLUKT]",
    "Bericht",
    "Verzenden",
    "Kanaal toevoegen",
    "Naam:",
    "bijv. #algemeen",
    "PSK (optioneel):",
    "base64-sleutel (leeg = openbaar)",
    "Toevoegen",
    "Voer kanaalnaam in",
    "Ongeldig, vol, of opslaan mislukt",
    "Alles als gelezen gemarkeerd",
    "Verlaten mislukt",
    "Genoemd: %s (willekeurige sleutel)",
    "Genoemd: geen (openbaar)",
    "Deze chat: %s",
    "Deze chat: geen",
    "Ongeldig bereik",
    "Bereik is niet opgeslagen",
    "Wissen mislukt; bereik blijft actief",
    "Genoemd routeringsbereik",
    "lokale naam (willekeurige sleutel)",
    "Instellen",
    "Wissen",
    "Kan DM niet openen: naam te lang",
    "Kan DM niet openen: gesprekslijst vol",

    nullptr,  // ReservedFallbackExample — falls back to English
};

constexpr const char* kItalian[string_count] = {
    // Home tiles (uppercase, 8px floor via the text-fit ladder)
    "CHAT",
    "DM",
    "STANZE",
    "CONTATTI",
    "RIPETITORI",
    "ANNUNCIA",
    "MAPPA",
    "TERMINALE",
    "PACCHETTI",
    "IMPOSTAZIONI",
    "CONFIGURA",
    "SEGNALE",
    "Configura radio",

    "Lingua",

    "PUBBLICO",
    "Ancora nessun messaggio",
    "Eliminare il canale?",
    "Eliminare il canale #%s?",
    "Annulla",
    "Elimina",
    "La rimozione del canale non è stata salvata",
    "Chat",
    "Aggiungi canale #",
    "Cerca nei messaggi...",
    "Carica messaggi più vecchi",
    "Torna ai più recenti",
    "Nessun messaggio corrispondente",
    "Emoji",
    "Chiudi",
    " [FALLITO]",
    "Messaggio",
    "Invia",
    "Aggiungi canale",
    "Nome:",
    "es. #generale",
    "PSK (facoltativo):",
    "chiave base64 (vuoto = pubblico)",
    "Aggiungi",
    "Inserisci il nome del canale",
    "Non valido, pieno o salvataggio non riuscito",
    "Tutti segnati come letti",
    "Uscita non riuscita",
    "Nominato: %s (chiave casuale)",
    "Nominato: nessuno (pubblico)",
    "Questa chat: %s",
    "Questa chat: nessuno",
    "Ambito non valido",
    "Ambito non salvato",
    "Cancellazione fallita; ambito ancora attivo",
    "Ambito di routing nominato",
    "nome locale (chiave casuale)",
    "Imposta",
    "Cancella",
    "Impossibile aprire DM: nome troppo lungo",
    "Impossibile aprire DM: elenco chat pieno",

    nullptr,  // ReservedFallbackExample — falls back to English
};

constexpr const char* kPortuguese[string_count] = {
    // Home tiles (uppercase, 8px floor via the text-fit ladder)
    "CHAT",
    "DMs",
    "SALAS",
    "CONTATOS",
    "REPETIDORES",
    "ANUNCIAR",
    "MAPA",
    "TERMINAL",
    "PACOTES",
    "AJUSTES",
    "CONFIGURAR",
    "SINAL",
    "Ajustar rádio",

    "Idioma",

    "PÚBLICO",
    "Ainda sem mensagens",
    "Excluir canal?",
    "Excluir o canal #%s?",
    "Cancelar",
    "Excluir",
    "A exclusão do canal não foi salva",
    "Chat",
    "Adicionar canal #",
    "Pesquisar mensagens...",
    "Carregar mensagens mais antigas",
    "Voltar às mais recentes",
    "Nenhuma mensagem correspondente",
    "Emoji",
    "Fechar",
    " [FALHOU]",
    "Mensagem",
    "Enviar",
    "Adicionar canal",
    "Nome:",
    "ex. #geral",
    "PSK (opcional):",
    "chave base64 (vazio = público)",
    "Adicionar",
    "Digite o nome do canal",
    "Inválido, cheio ou falha ao salvar",
    "Tudo marcado como lido",
    "Falha ao sair",
    "Nomeado: %s (chave aleatória)",
    "Nomeado: nenhum (público)",
    "Este chat: %s",
    "Este chat: nenhum",
    "Escopo inválido",
    "Escopo não salvo",
    "Falha ao limpar; escopo ainda ativo",
    "Escopo de roteamento nomeado",
    "nome local (chave aleatória)",
    "Definir",
    "Limpar",
    "Não foi possível abrir DM: nome muito longo",
    "Não foi possível abrir DM: lista de conversas cheia",

    nullptr,  // ReservedFallbackExample — falls back to English
};

const char* const* table_for(Language language)
{
    switch (language) {
    case Language::English: return kEnglish;
    case Language::German: return kGerman;
    case Language::French: return kFrench;
    case Language::Spanish: return kSpanish;
    case Language::Dutch: return kDutch;
    case Language::Italian: return kItalian;
    case Language::Portuguese: return kPortuguese;
    default: return nullptr;
    }
}

Language language_from_pref(uint8_t raw)
{
    return raw < language_count ? static_cast<Language>(raw) : Language::English;
}

bool g_initialized = false;
Language g_current = Language::English;

} // namespace

bool is_valid(Language language)
{
    return static_cast<size_t>(language) < language_count;
}

const char* language_name(Language language)
{
    return is_valid(language)
        ? kLanguageNames[static_cast<size_t>(language)]
        : kLanguageNames[static_cast<size_t>(Language::English)];
}

const char* tr_for(Language language, StringId id)
{
    const size_t index = static_cast<size_t>(id);
    if (index >= string_count) return kMissingTranslation;

    const char* const* table = table_for(language);
    const char* value = table ? table[index] : nullptr;
    if (!value || !value[0]) value = kEnglish[index];
    return value && value[0] ? value : kMissingTranslation;
}

const char* tr(StringId id)
{
    return tr_for(current_language(), id);
}

void init()
{
    if (g_initialized) return;
    g_current = language_from_pref(prefs_get().language);
    g_initialized = true;
}

Language current_language()
{
    init();
    return g_current;
}

bool set_language(Language language)
{
    if (!is_valid(language)) return false;
    init();

    NodePrefs prefs = prefs_get();
    prefs.language = static_cast<uint8_t>(language);
    if (!prefs_set(prefs)) return false;

    g_current = language;
    g_initialized = true;
    return true;
}

size_t copy_truncated(char* dest, size_t dest_size, StringId id)
{
    return utf8_copy_truncate(dest, dest_size, tr(id));
}

} // namespace sigurdos::i18n
