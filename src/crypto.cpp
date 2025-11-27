#include "common.h"

#include <condition_variable>
#include <mutex>

#include <boost/endian/arithmetic.hpp>
#include <boost/utility.hpp>

#include <libbz3.h>



/**
 * 已经处理过的 oid
 */
static Oid_set  crypto_set;



/**
 * 对象加密分两步
 * 1. 使用 bzip3 压缩数据
 * 2. 加官压缩后的数据
 *
 * 压缩层的数据格式, 分为 4 部分
 * 1.1. 原文件 git 哈希值的前 16 字节
 * 1.2. 原文件的长度
 * 1.3. 原文件压缩后数据
 * 1.4. 原文件 git 哈希值的后 16 字节
 *
 * 1.1 1.4 中的哈希值共保存 32 字节, 兼容 SHA1, SHA256, 当保存 SHA1 值时, 最后 12 字节为 0
 *
 * 1.2 的长度格式:
 *     第 1 字节的值 +1, 代表后面用几个字节保存长度数值, 最大值为 7, 代表后面有个 8 个字节
 *     后面的几字节以小端格式保存原文件长度的无符号数值
 *
 * 2 使用 AES 加密
 */



/**
 * 支持的最大文件 128M - 1 bytes
 */
static constexpr size_t  MAX_FILE = 128 * 1024 * 1024 - 1;


/**
 * BZIP3 最大分块大小, 32M, 再大对压缩率, 一般没有意义
 */
static constexpr size_t  MAX_BUFF = 16 + 9 + ( MAX_FILE + MAX_FILE/50 + 32 ) + 16 + 32;


/**
 * 压缩  缓冲区
 */
alignas( 32 ) static uint8_t  bzip_buff[MAX_BUFF];


/**
 * 原文,密文 缓冲区
 */
alignas( 32 ) static uint8_t  text_buff[MAX_BUFF];



/**
 * 最小的 BZIP3 块大小
 */
static constexpr size_t  MIN_BZIP3 = 65 * 1024;



static bz3_state  *bz3;



void init_crypt( )
{
   bz3 = bz3_new( MIN_BZIP3 );
   assert( bz3 != nullptr );
}



struct encrypt_element
{
   git_oid                   &oid;
   git_odb_object            *obj{};
   const uint8_t             *obj_data;
   size_t                     obj_size;
   std::vector< git_oid >     refs;
   bool                       second_pass{};

   View view( )
   {
      return View( obj_data, obj_size );
   }
};



static std::list< encrypt_element >     encrypt_stack;



static git_otype get_otype( const git_oid &oid )
{
   size_t     len;
   git_otype  otype;

   auto ret = git_odb_read_header( &len, &otype, odb, &oid );
   git_ensure( ret );

   return otype;
}



static void encrypt_have( git_otype otype, std::pair< const git_oid, const git_oid > *pair )
{
   trace( "encrypt ", otype, ' ', pair->first, "\n               ", pair->second );
   prog_num_2 = prog_num_2 + 1;
}



void encrypt( encrypt_element & );



void get_commit_refs( Oid_vec &refs, git_odb_object *obj )
{
   auto  sv = to_sv( obj );

   // tree
   {
      assert( sv.size( ) >= ( 4 + 1 + GIT_OID_HEXSZ + 1 ) );
      assert( sv[4 + 1 + GIT_OID_HEXSZ] == '\n' );
      assert( sv.starts_with( "tree " ) );

      auto  &oid = refs.emplace_back( );
      auto   ret = git_oid_fromstr( &oid, sv.data( ) + 5 );
      git_ensure( ret );

      sv.remove_prefix( 4 + 1 + GIT_OID_HEXSZ + 1 );
   }

   // parent
   while ( sv.starts_with( "parent " ) )
   {
      assert( sv.size( ) >= ( 6 + 1 + GIT_OID_HEXSZ + 1 ) );
      assert( sv[6 + 1 + GIT_OID_HEXSZ] == '\n' );

      auto  &oid = refs.emplace_back( );
      auto   ret = git_oid_fromstr( &oid, sv.data( ) + 7 );
      git_ensure( ret );

      sv.remove_prefix( 6 + 1 + GIT_OID_HEXSZ + 1 );
   }
}



void get_tree_refs( Oid_vec &refs, git_odb_object *obj )
{
   auto  sv = to_sv( obj );

   while ( !sv.empty( ) )
   {
      char  *sp;
      auto   mode = strtoul( sv.data( ), &sp, 8 );
      assert( *sp == ' ' );

      auto  nul = sv.find( '\0', sp + 1 - sv.data( ) );
      assert( nul != sv.npos );
      ++nul;

      assert( sv.size( ) >= ( nul + GIT_OID_RAWSZ ) );

      if ( mode != GIT_FILEMODE_COMMIT )
      {
         auto  &oid = refs.emplace_back( );
         git_oid_fromraw( &oid, reinterpret_cast< const uint8_t * >( sv.data( ) ) + nul );
      }

      sv.remove_prefix( nul + GIT_OID_RAWSZ );
   }
}



static void get_refs( Oid_vec &refs, git_odb_object *obj )
{
   // log_oid( *git_odb_object_id( obj ), "get_refs for : " );

   switch ( git_odb_object_type( obj ) )
   {
   case GIT_OBJ_COMMIT:  get_commit_refs( refs, obj );  return;
   case GIT_OBJ_TREE:    get_tree_refs( refs, obj );    return;
   case GIT_OBJ_BLOB:                                   return;
   default:              assert( false );               return;
   }

   // for ( auto &ref : refs )
   //    log_oid( ref, "   " );
}



static size_t encrypt_buff( const git_oid &oid, const uint8_t *data, size_t size )
{
   if ( size > MAX_FILE )
   {
      xcrypt_err( "file is too big, size = %zu", size );
      _Exit( EXIT_FAILURE );
   }

   uint8_t  *out = bzip_buff;

   // hash 1
   memcpy( out, oid.id, 16 );
   out += 16;

   // size
   {
      uint64_t  sz = size;

      for ( size_t i = 0; i < 8; ++i )
      {
         out[1+i] = static_cast< uint8_t >( sz );

         sz >>= 8;

         if ( sz == 0 )
         {
            out[0] = i;
            out += 2 + i;
            break;
         }
      }
   }

   // bzip3
   auto  bz3 = ::bz3;
   if ( size > MIN_BZIP3 )
   {
      bz3 = bz3_new( size );
      assert( bz3 != nullptr );
   }

   memcpy( out, data, size );
   auto  sz = bz3_encode_block( bz3, out, size );
   assert( sz > 0 );
   out += static_cast< uint32_t >( sz );

   if ( bz3 != ::bz3 )
      bz3_free( bz3 );

   // hash 2
   memcpy( out, oid.id + 16, 4 );
   memset( out + 4, 0, 12 );
   out += 16;

   return aes_encrypt( text_buff, bzip_buff, out - bzip_buff );
}



static void encrypt_commit( encrypt_element &top )
{
   static constexpr char author[] =
      "author git-remote-xcrypt <xxw_pc@163.com> 1713075873 +0800\n"
      "committer git-remote-xcrypt <xxw_pc@163.com> 1713075873 +0800\n\n";

   // 加密后的原提交
   auto    text_size = encrypt_buff( top.oid, top.obj_data, top.obj_size );

   // 新 commit 的大小
   size_t  need_size = top.refs.size( ) * ( 6 + 1 + 40 + 1 ) - 2;
   need_size += sizeof( author ) - 1;
   need_size += boost::beast::detail::base64::encoded_size( text_size );
   need_size += ( text_size - 1 ) / 48;

   if ( need_size > MAX_BUFF )
   {
      xcrypt_err( "commit is too big" );
      _Exit( EXIT_FAILURE );
   }

   // 在 new_buff 中构造加密 commit 文本
   Output< bzip_buff, sizeof( bzip_buff ) >  out;

   // tree
   out << "tree " << top.refs[0] << '\n';

   // parent
   for ( size_t i = 1; i < top.refs.size( ); ++i )
      out << "parent " << top.refs[i] << '\n';

   out << author;

   // 将 cipher_buff 中密文, 编码为 base64 追加到 text 后面, 每 64 base64 字节一行
   auto  ptr = text_buff;
   auto  end = text_buff + text_size;

   // 每 48 字节二进制密文, 编码为 64 字节 base64 字符
   for ( ; ( end - ptr ) > 48; ptr += 48 )
   {
      auto  sz = boost::beast::detail::base64::encode( out, ptr, 48 );
      assert( sz == 64 );

      out[64] = '\n';

      out += 65;
   }

   // 最后一行 base64
   auto  sz = boost::beast::detail::base64::encode( out, ptr, end - ptr );
   out += sz;

   //
   auto  ret = git_odb_write( &top.oid, odb, out.data( ), out.size( ), GIT_OBJ_COMMIT );
   git_ensure( ret );
}



static void encrypt_blob( encrypt_element &top );



static int get_width( size_t value )
{
   int  n = 0;

   do
   {
      ++n;
      value /= 10u;
   } while ( value != 0 );

   return n;
}



static void encrypt_tree( encrypt_element &top )
{
   encrypt_blob( top );

   auto     sv = to_sv( top.obj );

   int      width = get_width( top.refs.size( ) );

   Output< text_buff, MAX_FILE >  out;

   size_t  i = 0;
   while ( !sv.empty( ) )
   {
      char  *sp;
      auto   mode = strtoul( sv.data( ), &sp, 8 );
      assert( *sp == ' ' );

      auto   fn = sp - sv.data( ) + 1;

      auto   nul = sv.find( '\0', fn );
      assert( nul != sv.npos );
      ++nul;

      assert( sv.size( ) >= ( nul + GIT_OID_RAWSZ ) );

      if ( mode != GIT_FILEMODE_COMMIT )
      {
         if ( out.available( ) < ( fn + width + 1 + GIT_OID_RAWSZ ) )
         {
            xcrypt_err( "tree object is too big" );
            _Exit( EXIT_FAILURE );
         }

         out.append( sv.data( ), fn );

         auto  n = sprintf( out, "%0*zu", width, i );
         assert( n == width );
         out += n + 1;

         auto  &oid = top.refs[i];
         out.append( oid.id, GIT_OID_RAWSZ );

         ++i;
      }

      sv.remove_prefix( nul + GIT_OID_RAWSZ );
   }

   assert( i == top.refs.size( ) );

   if ( out.available( ) < ( 7 + width + 1 + GIT_OID_RAWSZ ) )
   {
      xcrypt_err( "tree object is too big" );
      _Exit( EXIT_FAILURE );
   }

   out << "100664 ";

   auto  n = sprintf( out, "%0*zu", width, top.refs.size( ) );
   assert( n == width );
   out += n + 1;

   out.append( top.oid.id, GIT_OID_RAWSZ );

   auto  ret = git_odb_write( &top.oid, odb, out.data( ), out.size( ), GIT_OBJ_TREE );
   git_ensure( ret );
}



static void encrypt_blob( encrypt_element &top )
{
   auto  text_size = encrypt_buff( top.oid, top.obj_data, top.obj_size );

   auto  ret = git_odb_write( &top.oid, odb, text_buff, text_size, GIT_OBJ_BLOB );
   git_ensure( ret );
}



void encrypt_object( encrypt_element &top )
{
   auto  pair = omp_find( top.oid );
   if ( pair != nullptr )
   {
      top.oid = pair->second;

      if ( crypto_set.emplace( pair->first ).second )
         encrypt_have( get_otype( pair->first ), pair );

      return;
   }

   auto  old_oid = top.oid;

   switch ( git_odb_object_type( top.obj ) )
   {
   case GIT_OBJ_COMMIT:   encrypt_commit( top );   break;
   case GIT_OBJ_TREE:     encrypt_tree  ( top );   break;
   case GIT_OBJ_BLOB:     encrypt_blob  ( top );   break;
   default:               assert( false );         break;
   }

   trace( "encrypt ", git_odb_object_type( top.obj ), ' ', old_oid, "\n             . ", top.oid );

   prog_num_1 = prog_num_1 + 1;
   omp_insert( old_oid, top.oid );
   crypto_set.emplace( old_oid );
}



static bool encrypt_push( git_oid &oid, git_otype otype )
{
   if ( crypto_set.contains( oid ) )
   {
      auto  pair = omp_find( oid );
      assert( pair != nullptr );

      oid = pair->second;

      return false;
   }

   encrypt_stack.emplace_back( oid );
   return true;
}



static bool encrypt_push_ref( encrypt_element &top )
{
   bool  rst = false;

   switch ( git_odb_object_type( top.obj ) )
   {
   case GIT_OBJ_COMMIT:
      assert( top.refs.size( ) > 0 );

      for ( auto &ref_oid : std::views::drop( top.refs, 1 ) )
      {
         auto  map = omp_find( ref_oid );
         if ( map != nullptr )
            ref_oid = map->second;

         else
         {
            if ( encrypt_push( ref_oid, GIT_OBJ_COMMIT ) )
               rst = true;
         }
      }

      if ( encrypt_push( top.refs.front( ), GIT_OBJ_TREE ) )
         rst = true;

      break;

   case GIT_OBJ_TREE:
      for ( auto &ref_oid : top.refs )
      {
         if ( encrypt_push( ref_oid, get_otype( ref_oid ) ) )
            rst = true;
      }
      break;
   }

   return rst;
}



static bool encrypt_element( encrypt_element &top )
{
   bool              succ = false;

   auto  ret = git_odb_read( &top.obj, odb, &top.oid );
   git_ensure( ret );

   top.obj_data = static_cast< const uint8_t * >( git_odb_object_data( top.obj ) );
   top.obj_size = git_odb_object_size( top.obj );

   if ( !top.second_pass )
   {
      top.second_pass = true;

      get_refs( top.refs, top.obj );

      if ( encrypt_push_ref( top ) )
         goto end;
   }

   encrypt_object( top );

   succ = true;

end:
   git_odb_object_free( top.obj );
   top.obj = nullptr;

   return succ;
}



static void encrypt_loop( )
{
   while ( !encrypt_stack.empty( ) )
   {
      auto  &top = encrypt_stack.back( );

      if ( encrypt_element( top ) )
      {
         assert( &top == &encrypt_stack.back( ) );
         encrypt_stack.pop_back( );
      }
      else
      {
         assert( &top != &encrypt_stack.back( ) );
      }
   }
}



void encrypt( git_revwalk *walk )
{
   assert( encrypt_stack.empty( ) );
   encrypt_stack.clear( );

   // 由于 encrypt_stack 里保存的都是 oid 引用, 需要 vec 保存所有原始 oid
   Oid_vec  vec;
   git_oid  oid;

   while ( git_revwalk_next( &oid, walk ) == 0 )
      vec.emplace_back( oid );

   // 将所有需要加密的 oid 无条件入栈
   for ( auto &oid : vec )
      encrypt_stack.emplace_back( oid );

   // 启动计数显示线程
   progress( PROG_ENCRYPT, 0, 0 );

   // 正式开始加密
   encrypt_loop( );

   progress_end_line( );
}



void encrypt( git_oid &oid )
{
   encrypt_stack.emplace_back( oid );
   encrypt_loop( );
}



static void decrypt( git_oid &oid, const uint8_t *data, size_t size, git_otype o_type )
{
   // 解密得到压缩层数据
   auto  bzip_size = aes_decrypt( bzip_buff, data, size );

   assert( bzip_size >= ( 16 + 2 + 8 + 16 ) );

   uint8_t  *ptr = bzip_buff;

   // file size
   ptr += 16;
   assert( ptr[0] <= 7 );

   size_t    file_size = 0;

   for ( size_t i = 0; i <= ptr[0]; ++i )
      file_size |= ptr[1+i] << ( i * 8 );

   ptr += 2 + ptr[0];

   assert( file_size <= MAX_FILE );

   // bzip3
   auto  bz3 = ::bz3;
   if ( file_size > MIN_BZIP3 )
   {
      bz3 = bz3_new( file_size );
      assert( bz3 != nullptr );
   }

   auto  sz = bzip_size - ( ptr - bzip_buff ) - 16;
   memcpy( text_buff, ptr, sz );
   auto  ret = bz3_decode_block( bz3, text_buff, sizeof( text_buff ), sz, file_size );
   assert( ret >= 0 );
   assert( static_cast< unsigned >( ret ) == file_size );

   if ( bz3 != ::bz3 )
      bz3_free( bz3 );

   ret = git_odb_write( &oid, odb, text_buff, file_size, o_type );
   git_ensure( ret );

   // 比较 hash
   assert( memcmp( bzip_buff, oid.id, 16 ) == 0 );

   ptr = bzip_buff + bzip_size - 16;
   assert( memcmp( ptr, oid.id + 16, 4 ) == 0 );

   for ( size_t i = 4; i < 16; ++i )
      assert( ptr[i] == 0 );
}



static void decrypt( git_oid &oid, const void *data, size_t size, git_otype o_type )
{
   decrypt( oid, static_cast< const uint8_t * >( data ), size, o_type );
}



static void decrypt_commit( git_oid &oid, git_odb_object *obj )
{
   auto  sv = to_sv( obj );

   auto  lf = sv.find( "\n\n" );
   assert( lf != sv.npos );
   sv.remove_prefix( lf + 2 );
   assert( sv.size( ) >= 64 );

   Output< text_buff, sizeof( text_buff ) >  out;

   // 将分行的 base64 解码到 data_buff
   auto  ptr = text_buff;

   while ( sv.size( ) > 64 )
   {
      assert( sv[64] == '\n' );

      auto  ret = boost::beast::detail::base64::decode( ptr, sv.data( ), 64 );
      assert( ret.first == 48 );
      ptr += 48;

      sv.remove_prefix( 65 );
   }

   assert( ( sv.size( ) % 4 ) == 0 );
   auto  ret = boost::beast::detail::base64::decode( ptr, sv.data( ), sv.size( ) );
   ptr += ret.first;

   decrypt( oid, text_buff, ptr - text_buff, GIT_OBJ_COMMIT );
}



static void decrypt_tree( git_oid &oid, git_odb_object *obj )
{
   auto  sv = to_sv( obj );

   assert( sv.size( ) > GIT_OID_RAWSZ );

   git_oid_fromraw( &oid, reinterpret_cast< const uint8_t * >( sv.data( ) ) + sv.size( ) - GIT_OID_RAWSZ );

   auto  ret = git_odb_read( &obj, odb, &oid );
   git_ensure( ret );

   decrypt( oid, git_odb_object_data( obj ), git_odb_object_size( obj ), GIT_OBJ_TREE );
}



static void decrypt_blob( git_oid &oid, git_odb_object *obj )
{
   decrypt( oid, git_odb_object_data( obj ), git_odb_object_size( obj ), GIT_OBJ_BLOB );
}



void decrypt( git_oid &oid, git_odb_object *obj )
{
   // 是否已经解密过
   auto  map = omp_find( oid );
   if ( map != nullptr )
   {
      trace( "decrypt ", git_odb_object_type( obj ), ' ', oid, "\n               ", map->second );
      oid = map->second;

      prog_num_2 = prog_num_2 + 1;
      return;
   }

   // 解密
   auto  old_oid = oid;

   switch ( git_odb_object_type( obj ) )
   {
   case GIT_OBJ_COMMIT:   decrypt_commit( oid, obj );   break;
   case GIT_OBJ_TREE:     decrypt_tree  ( oid, obj );   break;
   case GIT_OBJ_BLOB:     decrypt_blob  ( oid, obj );   break;
   default:               assert( false );         break;
   }

   trace( "decrypt ", git_odb_object_type( obj ), ' ', old_oid, "\n             . ", oid );

   prog_num_1 = prog_num_1 + 1;

   omp_insert( old_oid, oid );
}



void decrypt( git_revwalk *walk )
{
   git_odb_object         *obj;
   std::vector< git_oid >  refs;
   std::list< git_oid >    list;
   Oid_set                 set;

   //
   git_oid                 oid;

   while ( git_revwalk_next( &oid, walk ) == 0 )
   {
      list.emplace_back( oid );
      set.emplace( oid );
   }

   if ( list.empty( ) )
      return;

   // 启动计数显示线程
   progress( PROG_DECRYPT, 0, 0 );

   //
   while ( !list.empty( ) )
   {
      auto 	itr = list.begin( );

      auto  ret = git_odb_read( &obj, odb, &*itr );
      git_ensure( ret );

      refs.clear( );
      get_refs( refs, obj );

      if ( !refs.empty( ) )
      {
         switch ( git_odb_object_type( obj ) )
         {
         case GIT_OBJ_COMMIT:
            refs.resize( 1 );
            break;

         case GIT_OBJ_TREE:
            refs.pop_back( );
            break;

         default:
            assert( false );
         }

         for ( auto &ref_oid : refs )
         {
            if ( !set.contains( ref_oid ) )
            {
               list.emplace_front( ref_oid );
               set.emplace( ref_oid );
            }
         }
      }

      decrypt( *itr, obj );

      git_odb_object_free( obj );
      obj = nullptr;

      list.erase( itr );
   }

   progress_end_line( );
}



void decrypt( git_oid &oid )
{
   git_odb_object         *obj;

   auto ret = git_odb_read( &obj, odb, &oid );
   git_ensure( ret );

   decrypt( oid, obj );

   git_odb_object_free( obj );
}
