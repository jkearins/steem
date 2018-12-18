#pragma once

#include <mira/multi_index_container_fwd.hpp>
#include <mira/detail/object_cache.hpp>

#include <rocksdb/db.h>

#include <fc/io/raw.hpp>

namespace mira { namespace multi_index { namespace detail {

#define ID_INDEX 1

template< typename Value, typename Key, typename KeyFromValue,
          typename ID, typename IDFromValue >
struct rocksdb_iterator
{
   typedef typename std::shared_ptr< Value >       value_ptr;
   typedef object_cache<
      Value,
      ID,
      IDFromValue >                                cache_type;

private:
   const column_handles&                           _handles;
   const size_t                                    _index = 0;

   std::unique_ptr< ::rocksdb::Iterator >          _iter;
   std::shared_ptr< ::rocksdb::ManagedSnapshot >   _snapshot;
   ::rocksdb::ReadOptions                          _opts;
   db_ptr                                          _db;

   cache_type&                                     _cache;
   IDFromValue                                     _get_id;

public:

   rocksdb_iterator( const column_handles& handles, size_t index, db_ptr db, cache_type& cache ) :
      _handles( handles ),
      _index( index ),
      _db( db ),
      _cache( cache )
   {
      // Not sure the implicit move constuctor for ManageSnapshot isn't going to release the snapshot...
      //_snapshot = std::make_shared< ::rocksdb::ManagedSnapshot >( &(*_db) );
      //_opts.snapshot = _snapshot->snapshot();
      _iter.reset( _db->NewIterator( _opts, _handles[ _index ] ) );
   }

   rocksdb_iterator( const rocksdb_iterator& other ) :
      _handles( other._handles ),
      _index( other._index ),
      _snapshot( other._snapshot ),
      _db( other._db ),
      _cache( other._cache )
   {
      _iter.reset( _db->NewIterator( _opts, _handles[ _index] ) );

      if( other._iter->Valid() )
         _iter->Seek( other._iter->key() );
   }

   rocksdb_iterator( const column_handles& handles, size_t index, db_ptr db, cache_type& cache, const Key& k ) :
      _handles( handles ),
      _index( index ),
      _db( db ),
      _cache( cache )
   {
      //_snapshot = std::make_shared< ::rocksdb::ManagedSnapshot >( &(*_db) );
      //_opts.snapshot = _snapshot->snapshot();

      std::vector< char > ser_key = fc::raw::pack_to_vector( k );

      _iter.reset( _db->NewIterator( _opts, _handles[ _index ] ) );
      _iter->Seek( ::rocksdb::Slice( ser_key.data(), ser_key.size() ) );

      assert( _iter->status().ok() && _iter->Valid() );
   }

   rocksdb_iterator( const column_handles& handles, size_t index, db_ptr db, cache_type& cache, const ::rocksdb::Slice& s  ) :
      _handles( handles ),
      _index( index ),
      _db( db ),
      _cache( cache )
   {
      //_snapshot = std::make_shared< ::rocksdb::ManagedSnapshot >( &(*_db) );
      //_opts.snapshot = _snapshot->snapshot();

      _iter.reset( _db->NewIterator( _opts, _handles[ _index ] ) );
      _iter->Seek( s );

      assert( _iter->status().ok() && _iter->Valid() );
   }

   rocksdb_iterator( rocksdb_iterator&& other ) :
      _handles( other._handles ),
      _index( other._index ),
      _iter( std::move( other._iter ) ),
      _snapshot( other._snapshot ),
      _db( other._db ),
      _cache( other._cache )
   {
      //_opts.snapshot = _snapshot->snapshot();
      other._snapshot.reset();
      other._db.reset();
   }

   const Value& operator*()const
   {
      BOOST_ASSERT( valid() );
      ::rocksdb::Slice key_slice = _iter->value();
      std::shared_ptr< Value > ptr;
      ID id;

      if( _index == ID_INDEX )
      {
         fc::raw::unpack_from_char_array< ID >( key_slice.data(), key_slice.size(), id );
         ptr = _cache.get( id );

         if( !ptr )
         {
            // We are iterating on the primary key, so there is no indirection
            ptr = std::make_shared< Value >();
            fc::raw::unpack_from_char_array< Value >( key_slice.data(), key_slice.size(), *ptr );
            ptr = _cache.cache( std::move( *ptr ) );
         }
      }
      else
      {
         ::rocksdb::PinnableSlice value_slice;
         auto s = _db->Get( _opts, _handles[ ID_INDEX ], key_slice, &value_slice );
         assert( s.ok() );

         fc::raw::unpack_from_char_array< ID >( value_slice.data(), value_slice.size(), id );
         ptr = _cache.get( id );

         if( !ptr )
         {
            ptr = std::make_shared< Value >();
            fc::raw::unpack_from_char_array< Value >( value_slice.data(), value_slice.size(), *ptr );
            ptr = _cache.cache( std::move( *ptr ) );
         }
      }

      return (*ptr);
   }

   const Value* operator->()const
   {
      return &(**this);
   }

   rocksdb_iterator& operator++()
   {
      //BOOST_ASSERT( valid() );
      _iter->Next();
      assert( _iter->status().ok() );
      return *this;
   }

   rocksdb_iterator operator++(int)const
   {
      //BOOST_ASSERT( valid() );
      rocksdb_iterator new_itr( *this );
      return ++new_itr;
   }

   rocksdb_iterator& operator--()
   {
      _iter->Prev();
      assert( _iter->status().ok() );
      return *this;
   }

   rocksdb_iterator operator--(int)const
   {
      rocksdb_iterator new_itr( *this );
      return --new_itr;
   }

   bool valid()const
   {
      return _iter->Valid();
   }

   bool unchecked()const { return false; }

   bool equals( const rocksdb_iterator& other )const
   {
      if( _iter->Valid() && other._iter->Valid() )
      {
         ::rocksdb::Slice this_key = _iter->key();
         ::rocksdb::Slice other_key = other._iter->key();
         assert( this_key.size() == other_key.size() );
         return memcmp( this_key.data(), other_key.data(), this_key.size() ) == 0;
      }

      return _iter->Valid() == other._iter->Valid();
   }

   rocksdb_iterator& operator=( rocksdb_iterator&& other )
   {
      _iter = std::move( other._iter );
      _snapshot = other._snapshot;
      _db = other._db;
      return *this;
   }


   static rocksdb_iterator begin(
      const column_handles& handles,
      size_t index,
      db_ptr db,
      cache_type& cache )
   {
      rocksdb_iterator itr( handles, index, db, cache );
      itr._iter->SeekToFirst();
      return itr;
   }

   static rocksdb_iterator end(
      const column_handles& handles,
      size_t index,
      db_ptr db,
      cache_type& cache )
   {
      return rocksdb_iterator( handles, index, db, cache );
   }

   //template< typename Key >
   static rocksdb_iterator find(
      const column_handles& handles,
      size_t index,
      db_ptr db,
      cache_type& cache,
      const Key& k )
   {
      rocksdb_iterator itr( handles, index, db, cache );

      std::vector< char > ser_key = fc::raw::pack_to_vector( k );
      itr._iter->Seek( ::rocksdb::Slice( ser_key.data(), ser_key.size() ) );

      if( itr.valid() )
      {
         ::rocksdb::Slice found_key = itr._iter->key();
         if( memcmp( ser_key.data(), found_key.data(), std::min( ser_key.size(), found_key.size() ) ) != 0 )
         {
            itr._iter.reset( itr._db->NewIterator( itr._opts, itr._handles[ itr._index ] ) );
         }
      }
      else
      {
         itr._iter.reset( itr._db->NewIterator( itr._opts, itr._handles[ itr._index ] ) );
      }

      return itr;
   }

   //template< typename Key >
   static rocksdb_iterator lower_bound(
      const column_handles& handles,
      size_t index,
      db_ptr db,
      cache_type& cache,
      const Key& k )
   {
      rocksdb_iterator itr( handles, index, db, cache );

      std::vector< char > ser_key = fc::raw::pack_to_vector( k );
      itr._iter->Seek( ::rocksdb::Slice( ser_key.data(), ser_key.size() ) );

      return itr;
   }

   //template< typename Key >
   static rocksdb_iterator upper_bound(
      const column_handles& handles,
      size_t index,
      db_ptr db,
      cache_type& cache,
      const Key& k )
   {
      rocksdb_iterator itr( handles, index, db, cache );

      std::vector< char > ser_key = fc::raw::pack_to_vector( k );
      itr._iter->SeekForPrev( ::rocksdb::Slice( ser_key.data(), ser_key.size() ) );

      if( itr.valid() )
      {
         itr._iter->Next();
      }

      return itr;
   }

   template< typename LowerBoundType, typename UpperBoundType >
   static std::pair< rocksdb_iterator, rocksdb_iterator > range(
      const column_handles& handles,
      size_t index,
      db_ptr db,
      cache_type& cache,
      const LowerBoundType& lower,
      const UpperBoundType& upper )
   {
      return std::make_pair< rocksdb_iterator, rocksdb_iterator >(
         lower_bound( handles, index, db, cache, lower ),
         upper_bound( handles, index, db, cache, upper )
      );
   }
};

template< typename Value, typename Key, typename KeyFromValue,
          typename ID, typename IDFromValue >
bool operator==(
   const rocksdb_iterator< Value, Key, KeyFromValue, ID, IDFromValue >& x,
   const rocksdb_iterator< Value, Key, KeyFromValue, ID, IDFromValue >& y)
{
   return x.equals( y );
}

template< typename Value, typename Key, typename KeyFromValue,
          typename ID, typename IDFromValue >
bool operator!=(
   const rocksdb_iterator< Value, Key, KeyFromValue, ID, IDFromValue >& x,
   const rocksdb_iterator< Value, Key, KeyFromValue, ID, IDFromValue >& y)
{
   return !( x == y );
}


} } } // mira::multi_index::detail
