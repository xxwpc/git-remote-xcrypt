#include "common.h"



namespace
{
   class Aes_cbc_ctx
   {
   public:
      Aes_cbc_ctx( ) = default;

      ~Aes_cbc_ctx( )
      {
         if ( _ctx != nullptr )
            EVP_CIPHER_CTX_free( _ctx );
      }


      void reset( const EVP_CIPHER *cipher, const uint8_t *key, const uint8_t *iv, bool enc )
      {
         int   ret;

         if ( _ctx == nullptr ) [[unlikely]]
         {
            _ctx = EVP_CIPHER_CTX_new( );
            ensure( _ctx != nullptr );
         }
         else
         {
            ret = EVP_CIPHER_CTX_reset( _ctx );
            ssl_ensure( ret );
         }

         ret = EVP_CipherInit_ex( _ctx, cipher, NULL, key, iv, enc );
         ssl_ensure( ret );
      }


      size_t update( void *out_buff, const void *in_buff, size_t in_size )
      {
         ensure( in_size <= INT_MAX );

         int   out_size;
         auto  ret = EVP_CipherUpdate( _ctx, static_cast< unsigned char * >( out_buff ), &out_size,
               static_cast< const unsigned char * >( in_buff ), static_cast< int >( in_size ) );
         ssl_ensure( ret != 0 );

         return static_cast< unsigned >( out_size );
      }


      size_t finish( void *out_buff )
      {
         int   out_size;
         auto  ret = EVP_CipherFinal_ex( _ctx, static_cast< unsigned char * >( out_buff ), &out_size );
         ssl_ensure( ret );

         ret = EVP_CIPHER_CTX_cleanup( _ctx );
         ssl_ensure( ret );

         return static_cast< unsigned >( out_size );
      }


   private:
      EVP_CIPHER_CTX  *_ctx = nullptr;
   };
}


static Aes_cbc_ctx aes;



static void aes_first_block_by_passwd( uint8_t *out, const uint8_t *in, bool enc )
{
   aes.reset( EVP_aes_256_ecb( ), pw.key, nullptr, enc );

   auto  sz = aes.update( out, in, enc ? 16 : 32 );
   ensure( sz == 16 );
}



static size_t aes_cbc( const uint8_t *key, const uint8_t *iv, uint8_t *out_buff, const uint8_t *in_buff, size_t in_size, bool enc )
{
   aes.reset( EVP_aes_128_cbc( ), key, iv, enc );

   size_t  size = 0;

   while ( in_size > 0x7FFF'FFF0 ) [[unlikely]]
   {
      size += aes.update( out_buff, in_buff, 0x7FFF'FFF0 );

      out_buff += size;
      in_buff  += 0x7FFF'FFF0;
      in_size  -= 0x7FFF'FFF0;
   }

   size += aes.update( out_buff, in_buff, in_size );
   size += aes.finish( out_buff + size );

   return size;
}



static void crypt_first_block( uint8_t *out, const uint8_t *in, bool enc )
{
   aes_first_block_by_passwd( out, in, enc );
}



size_t aes_encrypt( uint8_t *out_buff, const uint8_t *in_buff, size_t in_size )
{
   ensure( in_size >= 32 );

   uint8_t  key[16];
   uint8_t  iv[16];

   memcpy( iv, in_buff, 16 );

   crypt_first_block( out_buff, in_buff, true );

   for ( size_t i = 0; i < 16; ++i )
      key[i] = iv[i] ^ out_buff[i];

   auto  size = aes_cbc( key, iv, out_buff + 16, in_buff + 16, in_size - 16, true );
   return size + 16;
}



size_t aes_decrypt( uint8_t *out_buff, const uint8_t *in_buff, size_t in_size )
{
   ensure( in_size >= 48 );
   ensure( ( in_size % 16 ) == 0 );

   uint8_t  key[16];
   memcpy( key, in_buff, 16 );

   crypt_first_block( out_buff, in_buff, false );

   for ( size_t i = 0; i < 16; ++i )
      key[i] ^= out_buff[i];

   auto  size = aes_cbc( key, out_buff, out_buff + 16, in_buff + 16, in_size - 16, false );
   return size + 16;
}
