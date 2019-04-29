#include <fc/crypto/elliptic_webauthn.hpp>
#include <fc/crypto/elliptic_r1.hpp>
#include <fc/crypto/base58.hpp>
#include <fc/crypto/openssl.hpp>

#include <fc/fwd_impl.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>

#include "rapidjson/reader.h"
#include <string>

namespace fc { namespace crypto { namespace webauthn {

namespace detail {
using namespace std::literals;

class public_key_impl {
   public:
      public_key_data data;
};

struct webauthn_json_handler : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, webauthn_json_handler> {
   std::string found_challenge;
   std::string found_origin;

   enum parse_stat_t {
      EXPECT_FIRST_OBJECT_START,
      EXPECT_FIRST_OBJECT_KEY,
      EXPECT_FIRST_OBJECT_DONTCARE_VALUE,
      EXPECT_CHALLENGE_VALUE,
      EXPECT_ORIGIN_VALUE,
      IN_NESTED_CONTAINER
   } current_state = EXPECT_FIRST_OBJECT_START;
   unsigned current_nested_container_depth = 0;

   bool Null() {
      return (current_state == IN_NESTED_CONTAINER || current_state == EXPECT_FIRST_OBJECT_DONTCARE_VALUE);
   }
   bool Bool(bool) {
      return (current_state == IN_NESTED_CONTAINER || current_state == EXPECT_FIRST_OBJECT_DONTCARE_VALUE);
   }
   bool Int(int) {
      return (current_state == IN_NESTED_CONTAINER || current_state == EXPECT_FIRST_OBJECT_DONTCARE_VALUE);
   }
   bool Uint(unsigned) {
      return (current_state == IN_NESTED_CONTAINER || current_state == EXPECT_FIRST_OBJECT_DONTCARE_VALUE);
   }
   bool Int64(int64_t) {
      return (current_state == IN_NESTED_CONTAINER || current_state == EXPECT_FIRST_OBJECT_DONTCARE_VALUE);
   }
   bool Uint64(uint64_t) {
      return (current_state == IN_NESTED_CONTAINER || current_state == EXPECT_FIRST_OBJECT_DONTCARE_VALUE);
   }
   bool Double(double) {
      return (current_state == IN_NESTED_CONTAINER || current_state == EXPECT_FIRST_OBJECT_DONTCARE_VALUE);
   }

   bool String(const char* str, rapidjson::SizeType length, bool copy) {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_KEY:
            return false;
         case EXPECT_CHALLENGE_VALUE:
            found_challenge = std::string(str, length);
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_ORIGIN_VALUE:
            found_origin = std::string(str, length);
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case IN_NESTED_CONTAINER:
            return true;
      }
   }

   bool StartObject() {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
            current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
            current_state = IN_NESTED_CONTAINER;
            ++current_nested_container_depth;
            return true;
         case IN_NESTED_CONTAINER:
            ++current_nested_container_depth;
            return true;
         case EXPECT_FIRST_OBJECT_KEY:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
            return false;
      }
   }
   bool Key(const char* str, rapidjson::SizeType length, bool copy) {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
            return false;
         case EXPECT_FIRST_OBJECT_KEY: {
            if("challenge"s == str)
               current_state = EXPECT_CHALLENGE_VALUE;
            else if("origin"s == str)
               current_state = EXPECT_ORIGIN_VALUE;
            else
               current_state = EXPECT_FIRST_OBJECT_DONTCARE_VALUE;
            return true;
         }
         case IN_NESTED_CONTAINER:
            return true;
      }
   }
   bool EndObject(rapidjson::SizeType memberCount) {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
            return false;
         case IN_NESTED_CONTAINER:
            if(!--current_nested_container_depth)
               current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
         case EXPECT_FIRST_OBJECT_KEY:
            return true;
      }
   }

   bool StartArray() {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
            current_state = IN_NESTED_CONTAINER;
            ++current_nested_container_depth;
            return true;
         case IN_NESTED_CONTAINER:
            ++current_nested_container_depth;
            return true;
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_KEY:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
            return false;
      }
   }
   bool EndArray(rapidjson::SizeType elementCount) {
      switch(current_state) {
         case EXPECT_FIRST_OBJECT_START:
         case EXPECT_FIRST_OBJECT_DONTCARE_VALUE:
         case EXPECT_CHALLENGE_VALUE:
         case EXPECT_ORIGIN_VALUE:
         case EXPECT_FIRST_OBJECT_KEY:
            return false;
         case IN_NESTED_CONTAINER:
            if(!--current_nested_container_depth)
               current_state = EXPECT_FIRST_OBJECT_KEY;
            return true;
      }
   }
};

}
}}}

namespace fc { namespace crypto { namespace webauthn {

bool public_key::valid() const {
   return true; ///XXX
}

public_key::public_key() {}
public_key::~public_key() {}
public_key::public_key(const public_key_data& dat) {
   my->data = dat;
}
public_key::public_key( const public_key& pk ) :my(pk.my) {}
public_key::public_key( public_key&& pk ) :my( fc::move( pk.my) ) {}
public_key& public_key::operator=( public_key&& pk ) {
   my = pk.my;
   return *this;
}
public_key& public_key::operator=( const public_key& pk ) {
   my = pk.my;
   return *this;
}

public_key_data public_key::serialize() const {
   return my->data;
}

public_key::public_key(const signature_data& c, const fc::sha256& digest, bool check_canonical) {
   fc::datastream<const char*> ds(c.data, c.size());

   fc::array<unsigned char, 65> compact_signature;
   std::vector<uint8_t> auth_data;
   std::string client_data;

   fc::raw::unpack(ds, compact_signature);
   fc::raw::unpack(ds, auth_data);
   fc::raw::unpack(ds, client_data);

   detail::webauthn_json_handler handler;
   rapidjson::Reader reader;
   rapidjson::StringStream ss(client_data.c_str());
   FC_ASSERT(reader.Parse(ss, handler), "Failed to parse client data JSON");

   std::string challenge_bytes = fc::base64url_decode(handler.found_challenge);
   FC_ASSERT(fc::sha256(challenge_bytes.data(), challenge_bytes.size()) == digest, "Wrong webauthn challenge");
   //XXX check origin here
   //XXX do we need to check rpid hash in auth_data?

   //the signature (and thus public key we need to return) will be over
   // sha256(auth_data || client_data_hash)
   fc::sha256 client_data_hash = fc::sha256::hash(client_data);
   fc::sha256::encoder e;
   e.write((char*)auth_data.data(), auth_data.size());
   e.write(client_data_hash.data(), client_data_hash.data_size());
   fc::sha256 signed_digest = e.result();

   //quite a bit of this copied ffrom elliptic_r1, can probably commonize
   int nV = compact_signature.data[0];
   if (nV<31 || nV>=35)
      FC_THROW_EXCEPTION( exception, "unable to reconstruct public key from signature" );
   ecdsa_sig sig = ECDSA_SIG_new();
   BIGNUM *r = BN_new(), *s = BN_new();
   BN_bin2bn(&compact_signature.data[1],32,r);
   BN_bin2bn(&compact_signature.data[33],32,s);
   ECDSA_SIG_set0(sig, r, s);

   fc::ec_key key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
   nV -= 4;

   if (r1::ECDSA_SIG_recover_key_GFp(key, sig, (uint8_t*)signed_digest.data(), signed_digest.data_size(), nV - 27, 0) == 1) {
      const EC_POINT* point = EC_KEY_get0_public_key(key);
      const EC_GROUP* group = EC_KEY_get0_group(key);
      size_t sz = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, (uint8_t*)my->data.data, my->data.size(), NULL);
      if(sz == my->data.size())
         return;
   }
   FC_THROW_EXCEPTION( exception, "unable to reconstruct public key from signature" );
}

}}}