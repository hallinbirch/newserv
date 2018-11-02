#include "ServerState.hh"

#include <memory>

#include "SendCommands.hh"
#include "Text.hh"

using namespace std;



ServerState::ServerState() : next_lobby_id(1), next_game_id(-1) {
  this->main_menu.emplace_back(MAIN_MENU_GO_TO_LOBBY, u"Go to lobby",
      u"Join the lobby.", 0);
  this->main_menu.emplace_back(MAIN_MENU_INFORMATION, u"Information",
      u"View server information.", MenuItemFlag::RequiresMessageBoxes);
  this->main_menu.emplace_back(MAIN_MENU_DISCONNECT, u"Disconnect",
      u"Disconnect.", 0);

  for (size_t x = 0; x < 15; x++) {
    shared_ptr<Lobby> l(new Lobby());
    l->flags |= LobbyFlag::Public | LobbyFlag::Default;
    this->add_lobby(l);
  }
  for (size_t x = 0; x < 5; x++) {
    shared_ptr<Lobby> l(new Lobby());
    l->flags |= LobbyFlag::Public | LobbyFlag::Default | LobbyFlag::Episode3;
    this->add_lobby(l);
  }
}

void ServerState::add_client_to_available_lobby(shared_ptr<Client> c) {
  rw_guard g(this->lobbies_lock, false);

  // nonnegative lobby IDs are public, so start at 0
  auto it = this->id_to_lobby.lower_bound(0);
  for (; it != this->id_to_lobby.end(); it++) {
    if (!(it->second->flags & LobbyFlag::Public)) {
      continue;
    }
    try {
      it->second->add_client(c);
      break;
    } catch (const out_of_range&) { }
  }

  if (it == this->id_to_lobby.end()) {
    throw out_of_range("all lobbies full");
  }

  // send a join message to the joining player, and notifications to all others
  this->send_lobby_join_notifications(it->second, c);
}

void ServerState::remove_client_from_lobby(shared_ptr<Client> c) {
  rw_guard g(this->lobbies_lock, false);

  auto l = this->id_to_lobby.at(c->lobby_id);
  l->remove_client(c);
  send_player_leave_notification(l, c->lobby_client_id);
}

void ServerState::change_client_lobby(shared_ptr<Client> c, shared_ptr<Lobby> new_lobby) {
  uint8_t old_lobby_client_id = c->lobby_client_id;

  shared_ptr<Lobby> current_lobby = this->find_lobby(c->lobby_id);
  try {
    if (current_lobby) {
      current_lobby->move_client_to_lobby(new_lobby, c);
    } else {
      new_lobby->add_client(c);
    }
  } catch (const out_of_range&) {
    send_lobby_message_box(c, u"$C6Can't change lobby\n\n$C7The lobby is full.");
    return;
  }

  if (current_lobby) {
    send_player_leave_notification(current_lobby, old_lobby_client_id);
  }
  this->send_lobby_join_notifications(new_lobby, c);
}

void ServerState::send_lobby_join_notifications(shared_ptr<Lobby> l,
    shared_ptr<Client> joining_client) {
  rw_guard g2(l->lock, false);
  for (auto& other_client : l->clients) {
    if (other_client == joining_client) {
      send_join_lobby(joining_client, l);
    } else {
      send_player_join_notification(other_client, l, joining_client);
    }
  }
}

shared_ptr<Lobby> ServerState::find_lobby(int64_t lobby_id) {
  rw_guard g(this->lobbies_lock, false);
  return this->id_to_lobby.at(lobby_id);
}

shared_ptr<Lobby> ServerState::find_lobby(const u16string& name) {
  rw_guard g(this->lobbies_lock, false);
  return this->name_to_lobby.at(name);
}

vector<shared_ptr<Lobby>> ServerState::all_lobbies() {
  rw_guard g(this->lobbies_lock, false);
  vector<shared_ptr<Lobby>> ret;
  for (auto& it : this->id_to_lobby) {
    ret.emplace_back(it.second);
  }
  return ret;
}

void ServerState::add_lobby(shared_ptr<Lobby> l) {
  if (l->is_game()) {
    l->lobby_id = this->next_game_id--;
  } else {
    l->lobby_id = this->next_lobby_id++;
  }

  rw_guard g(this->lobbies_lock, true);
  if (this->id_to_lobby.count(l->lobby_id)) {
    throw logic_error("lobby already exists with the given id");
  }
  if (this->name_to_lobby.count(l->name)) {
    throw invalid_argument("lobby already exists with the given name");
  }
  this->id_to_lobby.emplace(l->lobby_id, l);
  if (l->name[0]) {
    this->name_to_lobby.emplace(l->name, l);
  }
}

void ServerState::remove_lobby(int64_t lobby_id) {
  rw_guard g(this->lobbies_lock, true);
  auto it = this->id_to_lobby.find(lobby_id);
  if (it == this->id_to_lobby.end()) {
    return;
  }
  if (it->second->name[0]) {
    this->name_to_lobby.erase(it->second->name);
  }
  this->id_to_lobby.erase(it);
}

shared_ptr<Client> ServerState::find_client(const char16_t* identifier,
    uint64_t serial_number, shared_ptr<Lobby> l) {

  if ((serial_number == 0) && identifier) {
    try {
      string encoded = encode_sjis(identifier);
      serial_number = stoull(encoded, NULL, 0);
    } catch (const exception&) { }
  }

  // look in the current lobby first
  if (l) {
    try {
      return l->find_client(identifier, serial_number);
    } catch (const out_of_range&) { }
  }

  // look in all lobbies if not found
  for (auto& other_l : this->all_lobbies()) {
    if (l == other_l) {
      continue; // don't bother looking again
    }
    try {
      return other_l->find_client(identifier, serial_number);
    } catch (const out_of_range&) { }
  }

  throw out_of_range("client not found");
}