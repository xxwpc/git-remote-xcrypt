/**
 * Copyright 2026 Xiao Xuanwen <xxw_pc@163.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common.h"



void repo_close( )
{
   if ( repo == nullptr )
      return;

   git_odb_free( odb );
   git_repository_free( repo );

   repo = nullptr;
   odb  = nullptr;
}



/**
 * 获取远程密钥在配置中的名称
 */
std::string get_secret_key_config_name( const char *remote_name )
{
   std::string    name = "remote.";
   name += remote_name;
   name += ".xcrypt-secret-key";
   return name;
}



void check_secret_key_format( const char *secret_key )
{
   std::string_view  key( secret_key );
   if ( !key.starts_with( "psw:" ) )
      xcrypt_abort( "The secret key format is incorrect" );

   if ( key.size( ) == 4 )
      xcrypt_abort( "The secret key is empty" );
}



void load_remote( const char *rename_name )
{
   ::remote_name = rename_name;

   git_config  *cfg;

   auto  ret = git_repository_config_snapshot( &cfg, repo );
   git_ensure( ret );

   // cfg name
   auto  name = get_secret_key_config_name( remote_name );

   // 密码
   const char *key;
   ret = git_config_get_string( &key, cfg, name.c_str( ) );
   if ( ret != 0 )
      xcrypt_abort( "Can't get remote secret key" );

   check_secret_key_format( key );

   // 计算密码的 SHA256
   sha3_256( pw.md, key + 4, strlen( key + 4 ) );
}
