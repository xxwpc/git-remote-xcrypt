#include "common.h"

#include <filesystem>
#include <regex>
#include <typeinfo>
#include <vector>

#include <fcntl.h>
#include <termios.h>

#include <boost/preprocessor/arithmetic/sub.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/variadic/size.hpp>


git_repository   *repo;
git_odb          *odb;

Password          pw;

int               log_indent;

const char       *git_dir;
const char       *remote_name;
const char       *remote_url;


std::string       refs_prefix;

git_direction     direction;
git_remote       *remote;
git_transport    *transport;
const git_remote_head  **heads;
size_t                   heads_count;



/**
 * output, 将信息同时输出到 trace 及 stdout
 *
 * 当 output 带参数时, 其参数格式与 printf 一致
 * 当 output 无参数时, 将输出一空行, 并刷新 stdout
 *
 *
 * 注:
 * 在本宏的实现中, 使用 BOOST_PP_IF 判断有无参数, 但是当调用者没有参数时, 如
 *    output( )
 *
 * 此时 __VA_ARGS__ 并不是真的空, 而是包含一个看不见的空白占位符, 它会导致
 * BOOST_PP_VARIADIC_SIZE( __VA_ARGS__ ) 返回 1
 *
 * 使用 ( 0, ##__VA_ARGS__ ) 来消除这个占位符, 此时
 *    output( )     展开为  0       使用 BOOST_PP_VARIADIC_SIZE 获取参数个数为 1
 *    output( "" )  展开为  0, ""   使用 BOOST_PP_VARIADIC_SIZE 获取参数个数为 2
 *
 * 但 BOOST_PP_IF 条件为 0 与 非0, 故再使用 BOOST_PP_SUB 减个 1
 * 就可以完美区分 output 宏是有参数
 */
static char    _output_buf[8192];

#define output( ... ) \
   BOOST_PP_IF( BOOST_PP_SUB( BOOST_PP_VARIADIC_SIZE( 0, ##__VA_ARGS__ ), 1 ), \
   ({ \
      snprintf( _output_buf, sizeof( _output_buf ), ##__VA_ARGS__ ); \
      trace( "output       : ", _output_buf ); \
      puts( _output_buf ); \
   }), \
   ({ \
      trace( "output       :" ); \
      puts( "" ); \
      fflush( stdout ); \
   })) \



static std::string get_xcrypt_remote_ref( const char *name )
{
   return std::string( "refs/xcrypt/remotes/" ) + remote_name + "/" + ( name + 5 );
}



static std::string get_xcrypt_local_ref( const char *name )
{
   return std::string( "refs/xcrypt/local/" ) + ( name + 5 );
}



static int transport_message( const char *str, int len, void *payload )
{
   fprintf( stderr, "\033[K\rremote: %*.*s", len, len, str );
   fflush( stderr );
   return 0;
}



/**
 * 从终端读取密码, 不回显
 *
 * - 通过 `/dev/tty` 读取
 * - 读取前后清空 tty 缓存（tcflush）
 * - 按字节读取，直到遇到 '\n' 或 '\r'
 */
static std::string read_password( const std::string_view &prompt )
{
   fprintf( stderr, "%s's password: ", prompt.data( ) );
   fflush( stderr );

   int tty_fd = open( "/dev/tty", O_RDWR | O_CLOEXEC );
   if ( tty_fd < 0 )
      xcrypt_abort( "open /dev/tty failed: %s", strerror( errno ) );

   // 读取前清空缓存，避免残留输入（例如上一次回车）直接被读走
   (void)tcflush( tty_fd, TCIOFLUSH );

   struct termios oldt;
   if ( tcgetattr( tty_fd, &oldt ) != 0 )
   {
      close( tty_fd );
      xcrypt_abort( "tcgetattr failed: %s", strerror( errno ) );
   }

   struct termios newt = oldt;
   newt.c_lflag &= static_cast< tcflag_t >( ~ECHO );

   if ( tcsetattr( tty_fd, TCSANOW, &newt ) != 0 )
   {
      (void)tcsetattr( tty_fd, TCSANOW, &oldt );
      close( tty_fd );
      xcrypt_abort( "tcsetattr failed: %s", strerror( errno ) );
   }

   std::string password;

   while ( true )
   {
      char ch;
      const auto n = read( tty_fd, &ch, 1 );
      if ( n == 0 )
         break;

      if ( n < 0 )
      {
         if ( errno == EINTR )
            continue;

         (void)tcsetattr( tty_fd, TCSANOW, &oldt );
         close( tty_fd );
         xcrypt_abort( "read /dev/tty failed: %s", strerror( errno ) );
      }

      if ( ch == '\n' || ch == '\r' )
         break;

      password.push_back( ch );
   }

   // 恢复回显
   (void)tcsetattr( tty_fd, TCSANOW, &oldt );

   // 读取后再次清空缓存，避免残留（例如 Windows 风格 \r\n 的另一半）影响后续读取
   (void)tcflush( tty_fd, TCIOFLUSH );

   close( tty_fd );

   // 因为没有回显，读取密码遇到回车结束后，那个回车不会显示在终端上，所以这里补一个换行
   fprintf( stderr, "\n" );
   return password;
}



/**
 * 需求:
 *  - 该函数会被多次调用；每次返回一个证书(私钥)或明文密码
 *  - 若 ~/.ssh 存在多个证书，则依次尝试
 *  - 所有证书都失败后，最后返回明文密码
 */
static int ssh_cred_acquire( git_cred **cred, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload )
{
   (void)url;

   ensure( payload != nullptr );
   auto  &idx = *static_cast< unsigned * >( payload );

   // 依次尝试的证书列表
   static constexpr const char *cred_names[] =
   {
      "id_rsa",
      "id_ecdsa",
      "id_ecdsa_sk",
      "id_ed25519",
      "id_ed25519_sk",
   };

   // 优先尝试 ~/.ssh 目录下的证书
   if ( ( ( allowed_types & GIT_CREDTYPE_SSH_KEY ) != 0 ) && ( idx < std::size( cred_names ) ) )
   {
      // 获取家目录
      auto home = my_getenv( "HOME" );

      // 如果家目录不存在，则跳过 ~/.ssh 下的证书，尝试明文密码
      if ( home == nullptr )
         idx = std::size( cred_names );

      // 依次尝试 ~/.ssh 下的证书
      else
      {
         std::filesystem::path   key_path;

         while ( idx < std::size( cred_names ) )
         {
            key_path  = home;
            key_path /= ".ssh";
            key_path /= cred_names[idx];
            ++idx;

            if ( !std::filesystem::exists( key_path ) )
               continue;

            auto ret = git_cred_ssh_key_new( cred, username_from_url, nullptr, key_path.c_str( ), nullptr );
            if ( ret == 0 )
               return 0;
         }
      }
   }

   // 证书用尽（或不允许 SSH_KEY）后：最后回退到明文密码
   if ( ( ( allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT ) != 0 ) && ( idx >= std::size( cred_names ) ) )
   {
      // 如果不是第一次尝试明文密码，则表示已经失败过一次，提示权限被拒绝
      if ( idx != std::size( cred_names ) )
         fprintf( stderr, "Permission denied, please try again.\n" );

      ++idx;

      // url 形如: ssh://username@hostname:port/path/to/repo.git
      //     或          username@hostname:port/path/to/repo.git
      // 读取密码前，需要提取提示字符串 username@hostname

      std::string  prompt = url;

      if ( prompt.starts_with( "ssh://" ) )
         prompt.erase( 0, 6 );

      if ( auto pos = prompt.find( ':' ); pos != std::string::npos )
         prompt.resize( pos );

      // 读取密码
      std::string  password = read_password( prompt );

      auto ret = git_cred_userpass_plaintext_new( cred, username_from_url, password.c_str( ) );
      if ( ret == 0 )
         return 0;
   }

   return GIT_PASSTHROUGH;
}



static int transfer_progress( const git_transfer_progress *stats, void *payload )
{
   if ( stats->total_deltas == 0 )
      return progress( PROG_RECEIVE, stats->received_objects, stats->total_objects );

   else
      return progress( PROG_UNPACK, stats->indexed_deltas, stats->total_deltas );
}



static int pack_progress( int stage, uint32_t current, uint32_t total, void *payload )
{
   return progress( PROG_ENUMERATE + stage, current, total );
}



static int push_transfer_progress( unsigned int current, unsigned int total, size_t bytes,  void* payload )
{
   return progress( PROG_WRITE, current, total );
}



static int push_update_ref( const char *refname, const char *status, void *data )
{
   ensure( status == nullptr );

   output( "ok %s", refname );

   // 成功后, 将本地加密引用, 移动到远程加密引用
   git_reference  *old_ref;
   git_reference  *new_ref;

   auto  ret = git_reference_lookup( &old_ref, repo, get_xcrypt_local_ref( refname ).c_str( ) );
   git_ensure( ret );

   ret = git_reference_rename( &new_ref, old_ref, get_xcrypt_remote_ref( refname ).c_str( ), 1, nullptr );
   git_ensure( ret );

   git_reference_free( old_ref );
   git_reference_free( new_ref );

   return 0;
}



/**
 * ssh_cred_acquire 函数中, 如果 ~/.ssh 中存在多少证书, 则依次尝试多少次
 * 在每次尝试时, cred_index 会自增
 * 在每次连接开始前, 将 cred_index 重置为 0
 */
static unsigned cred_index;



static constexpr git_remote_callbacks  remote_cb =
{
   .version                = GIT_REMOTE_CALLBACKS_VERSION,
   .sideband_progress      = transport_message,
   .credentials            = ssh_cred_acquire,
   .transfer_progress      = transfer_progress,
   .pack_progress          = pack_progress,
   .push_transfer_progress = push_transfer_progress,
   .push_update_reference  = push_update_ref,
   .payload                = &cred_index,
};



static constexpr git_proxy_options  proxy_opts =
{
   .version = GIT_PROXY_OPTIONS_VERSION,
};



#if LIBGIT2_NUMBER >= 10400
static constexpr git_remote_connect_options connect_opts =
{
   .version    = GIT_REMOTE_CONNECT_OPTIONS_VERSION,
   .callbacks  = remote_cb,
   .proxy_opts = proxy_opts,
};
#endif



static void repo_open( )
{
   if ( repo != nullptr )
      return;

   auto  ret = git_repository_open( &repo, git_dir );
   git_ensure( ret );

   ret = git_repository_odb( &odb, repo );
   git_ensure( ret );
}



static void repo_open_cur_dir( )
{
   if ( repo != nullptr )
      return;

   auto  ret = git_repository_open_ext( &repo, ".", 0, nullptr );
   git_ensure( ret );

   git_dir = strdup( git_repository_path( repo ) );

   ret = git_repository_odb( &odb, repo );
   git_ensure( ret );
}



std::string                      stdin_line;
std::vector< std::string_view >  argv;


auto & read_input( )
{
   get_line( stdin, stdin_line );

   trace( "input  : ", stdin_line );

   std::string_view  sv( stdin_line );
   boost::split( argv, sv, isspace, boost::token_compress_on );

   ensure( !argv.empty( ) );
   if ( argv.back( ).empty( ) )
      argv.pop_back( );

   return argv;
}



FILE *ssh;



static void do_capabilities( )
{
   output( "fetch" );
   output( "push" );
   output( );
}



static git_transport * connect_fetch( )
{
   git_transport  *t;

   auto ret = git_transport_new( &t, remote, remote_url );
   git_ensure( ret );

#if LIBGIT2_NUMBER >= 10500
   cred_index = 0;
   ret = t->connect( t, remote_url, GIT_DIRECTION_FETCH, &connect_opts );
   git_ensure( ret );
#else
   t->set_callbacks( t, transport_message, nullptr, nullptr, nullptr );

   ret = t->connect( t, remote_url, ssh_cred_acquire, nullptr, nullptr, GIT_DIRECTION_FETCH, 0 );
   git_ensure( ret );
#endif

   return t;
}



static void do_list_fetch( )
{
   transport = connect_fetch( );

   auto  ret = transport->ls( &heads, &heads_count, transport );
   git_ensure( ret );
}



static void do_list_push( )
{
   // 连接之前, 重置
   cred_index = 0;

#if LIBGIT2_NUMBER >= 10400
   auto  ret = git_remote_connect_ext( remote, GIT_DIRECTION_PUSH, &connect_opts );
   git_ensure( ret );
#else
   auto  ret = git_remote_connect( remote, GIT_DIRECTION_PUSH, &remote_cb, nullptr, nullptr );
   git_ensure( ret );
#endif

   ret = git_remote_ls( &heads, &heads_count, remote );
   git_ensure( ret );
}



static void fetch_head( )
{
   int   ret;

   // 收集需要 fetch 的 head
   std::vector< const git_remote_head * >    need_heads;

   for ( auto h : std::span< const git_remote_head * >( heads, heads_count ) )
   {
      if ( git_odb_exists( odb, &h->oid ) )
         continue;

      // 不下载符号链接
      if ( h->symref_target != nullptr )
         continue;

      need_heads.emplace_back( h );
   }

   // 无需下载
   if ( need_heads.empty( ) )
      return;

   // 对于 push 方向来说, 远程启动的是 git-receive-pack, 它不支持 fetch 操作
   // 所以要重启一个 transport 来下载

   auto  tp = ::transport;

   if ( tp == nullptr )
      tp = connect_fetch( );

   // 开始下载
#if LIBGIT2_NUMBER >= 10700
   git_fetch_negotiation  fetch_nego =
   {
      .refs     = need_heads.data( ),
      .refs_len = need_heads.size( ),
      // 1.7.2 设置 depth 后, fetch 会失败, 原因不明
      // .depth    = 1,
   };

   ret = tp->negotiate_fetch( tp, repo, &fetch_nego );
#else
   ret = tp->negotiate_fetch( tp, repo, need_heads.data( ), need_heads.size( ) );
#endif
   git_ensure( ret );

   git_transfer_progress stats;
#if LIBGIT2_NUMBER >= 10500
   ret = tp->download_pack( tp, repo, &stats );
#else
   ret = tp->download_pack( tp, repo, &stats, &transfer_progress, nullptr );
#endif
   git_ensure( ret );

   if ( tp != ::transport )
   {
      tp->close( tp );
      tp->free ( tp );
   }
}



static void decrypt_fetch( )
{
   git_revwalk  *walk;

   auto  ret = git_revwalk_new( &walk, repo );
   git_ensure( ret );

   for ( auto h : std::span< const git_remote_head * >( heads, heads_count ) )
   {
      ret = git_revwalk_push( walk, &h->oid );
      git_ensure( ret );
   }

   std::string    glob( "refs/xcrypt/remotes/" );
   glob += remote_name;

   ret = git_revwalk_hide_glob( walk, glob.c_str( ) );
   git_ensure( ret );

   decrypt( walk );

   git_revwalk_free( walk );
}



static void do_list_result( )
{
   for ( auto h : std::span< const git_remote_head * >( heads, heads_count ) )
   {
      if ( h->symref_target != nullptr )
         output( "@%s %s", h->symref_target, h->name );

      else
      {
         auto  ref_name = get_xcrypt_remote_ref( h->name );

         git_reference  *ref;
         auto ret = git_reference_create( &ref, repo, ref_name.c_str( ), &h->oid, 1, nullptr );
         git_ensure( ret );
         git_reference_free( ref );

         trace( "xcrypt remote  ", h->oid, " ", ref_name );

         auto  map = omp_find( h->oid );
         if ( map == nullptr )
         {
            git_oid  oid = h->oid;
            decrypt( oid );
            map = omp_find( h->oid );
            ensure( map != nullptr );
         }

         char  str[GIT_OID_HEXSZ+1];
         git_oid_tostr( str, GIT_OID_HEXSZ+1, &map->second );

         output( "%s %s", str, h->name );
      }
   }

   output( );
}



static void do_list( )
{
   repo_open( );

   auto  ret = git_remote_create_anonymous( &remote, repo, remote_url );
   git_ensure( ret );

   // fetch 没有参数
   // push  有一个 for-push 的参数
   // 所以使用 argv.size( ) 来判断是 fetch 还是 push
   if ( argv.size( ) == 1 )
      do_list_fetch( );
   else
      do_list_push( );

   fetch_head( );

   decrypt_fetch( );

   do_list_result( );
}



static void do_fetch( )
{
   git_oid  oid;

   do
   {
      auto  ret = git_oid_fromstr( &oid, argv[1].data( ) );
      git_ensure( ret );

      ensure( git_odb_exists( odb, &oid ) != 0 );

   } while ( !read_input( ).empty( ) );

   output( );
}



static constexpr git_push_options  push_opts =
{
   .version    = GIT_PUSH_OPTIONS_VERSION,
   .callbacks  = remote_cb,
   .proxy_opts = proxy_opts,
};



static void do_push( )
{
   repo_open( );

   git_revwalk  *walk;
   auto ret = git_revwalk_new( &walk, repo );
   git_ensure( ret );

   std::list< std::tuple< bool, git_oid *, std::string > >    refspec_list;

   std::regex     rgx( "\\+?([^:]*):(.+)" );
   std::cmatch    m;

   do
   {
      auto  &orig_refspec = argv[1];
      ensure( std::regex_match( orig_refspec.begin( ), orig_refspec.end( ), m, rgx ) );

      auto  &m1 = m[1];
      auto  &m2 = m[2];

      auto  force = orig_refspec.front( ) == '+';

      auto  oid = static_cast< git_oid * >( nullptr );

      if ( m1.length( ) > 0 )
      {
         git_object  *obj;
         auto  ret = git_revparse_single( &obj, repo, m1.str( ).c_str( ) );
         git_ensure( ret );

         oid = new git_oid{ *git_object_id( obj ) };
         ret = git_revwalk_push( walk, oid );
         git_ensure( ret );

         git_object_free( obj );
      }

      auto  &rs = refspec_list.emplace_back( force, oid, m2.first );
      trace( "push hash      ", ( std::get<0>( rs ) ? "+" : "" ), *std::get<1>( rs ), ":", std::get<2>( rs ) );

   } while ( !read_input( ).empty( ) );

   // 去除远程仓库已有的提交
   std::string    remote_dir = "refs/remotes/";
   remote_dir += remote_name;

   ret = git_revwalk_hide_glob( walk, remote_dir.c_str( ) );
   git_ensure( ret );

   // 加密
   encrypt( walk );

   //
   git_revwalk_free( walk );

   //
   GitStrArray    arr;
   std::string    refspec;

   for ( auto &rs : refspec_list )
   {
      auto  oid = std::get< 1 >( rs );

      if ( oid == nullptr )
      {
         refspec  = ":";
         refspec += std::get<2>( rs );
      }
      else
      {
         auto  omp = omp_find( *oid );
         ensure( omp != nullptr );

         trace( "push encrypt   ", ( std::get<0>( rs ) ? "+" : "" ), omp->second, ":", std::get<2>( rs ) );

         if ( std::get< 0 >( rs ) )
            refspec += '+';

         auto  local_ref = get_xcrypt_local_ref( std::get<2>( rs ).c_str( ) );
         refspec += local_ref;
         refspec += ':';
         refspec += std::get<2>( rs );

         git_reference  *ref;
         ret = git_reference_create( &ref, repo, local_ref.c_str( ), &omp->second, 1, nullptr );
         git_ensure( ret );
         git_reference_free( ref );

         delete oid;
      }

      trace( "push libgit2   ", refspec );
      arr.push( std::move( refspec ) );
   }

   cred_index = 0;
   ret = git_remote_upload( remote, arr, &push_opts );
   git_ensure( ret );

   output( );
}



static void do_quit( )
{
}



static constexpr std::pair< std::string_view, void (*)( ) >  command_table[] =
{
   { "",              &do_quit },
   { "capabilities",  &do_capabilities },
   { "fetch",         &do_fetch },
   { "list",          &do_list },
   { "push",          &do_push },
};



static void (*find_command_handler( ))( )
{
   auto  itr = std::lower_bound( std::begin( command_table ), std::end( command_table ), argv[0],
      []( auto &item, const std::string_view &cmd )
      {
         return item.first < cmd;
      } );

   if ( itr == std::end( command_table ) )
      return nullptr;

   if ( itr->first != argv[0] )
      return nullptr;

   return itr->second;
}



int remote_helper( unsigned argc, char **argv )
{
   trace( "GIT_DIR = ", git_dir );

   if ( argc != 3 )
   {
      xcrypt_err( "input %d parameters, only support 3 parameters.", argc );
      exit( EXIT_FAILURE );
   }

   remote_name = argv[1];
   remote_url  = argv[2];

   repo_open( );

   load_remote( argv[1] );

   omp_load( );
   atexit( omp_store );

   while ( !read_input( ).empty( ) )
   {
      auto handler = find_command_handler( );
      if ( handler == nullptr )
      {
         xcrypt_err( "Can't find handler for command '%s'", stdin_line.c_str( ) );
         break;
      }

      handler( );

      if ( handler == do_quit )
         break;
   }

   return EXIT_SUCCESS;
}
