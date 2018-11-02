#include "NetworkAddresses.hh"

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>

#include <memory>
#include <phosg/Encoding.hh>
#include <phosg/Strings.hh>
#include <stdexcept>

using namespace std;



uint32_t resolve_address(const char* address) {
  struct addrinfo *res0;
  if (getaddrinfo(address, NULL, NULL, &res0)) {
    auto e = string_for_error(errno);
    throw runtime_error(string_printf("can\'t resolve hostname %s: %s", address,
        e.c_str()));
  }

  std::unique_ptr<struct addrinfo, void(*)(struct addrinfo*)> res0_unique(
      res0, freeaddrinfo);
  struct addrinfo *res4 = NULL;
  for (struct addrinfo* res = res0; res; res = res->ai_next) {
    if (res->ai_family == AF_INET) {
      res4 = res;
    }
  }
  if (!res4) {
    throw runtime_error(string_printf(
        "can\'t resolve hostname %s: no usable data", address));
  }

  struct sockaddr_in* res_sin = (struct sockaddr_in*)res4->ai_addr;
  return res_sin->sin_addr.s_addr;
}

set<uint32_t> get_local_address_list() {
  struct ifaddrs* ifa_raw;
  if (getifaddrs(&ifa_raw)) {
    auto s = string_for_error(errno);
    throw runtime_error(string_printf("failed to get interface addresses: %s", s.c_str()));
  }

  unique_ptr<struct ifaddrs, void(*)(struct ifaddrs*)> ifa(ifa_raw, freeifaddrs);

  set<uint32_t> ret;
  for (struct ifaddrs* i = ifa.get(); i; i = i->ifa_next) {
    if (!i->ifa_addr) {
      continue;
    }

    auto* sin = reinterpret_cast<sockaddr_in*>(i->ifa_addr);
    if (sin->sin_family != AF_INET) {
      continue;
    }

    ret.emplace(bswap32(sin->sin_addr.s_addr));
  }

  return ret;
}

bool is_local_address(uint32_t addr) {
  uint8_t net = addr & 0xFF;
  if ((net != 127) && (net != 172) && (net != 10) && (net != 192)) {
    return false;
  }
  return true;
}