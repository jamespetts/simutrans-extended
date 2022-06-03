/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef TPL_SPARSE_TPL_H
#define TPL_SPARSE_TPL_H


#include "../dataobj/koord.h"
#include "../macros.h"
#include "../simtypes.h"


template <class T, typename IDX=uint16> class sparse_tpl;
template<class T, typename IDX> void swap(sparse_tpl<T,IDX>& a, sparse_tpl<T,IDX>& b);

/**
 * A template class for spares 2-dimensional arrays.
 * It's using Compressed Row Storage (CRS).
 * @see array2d_tpl
 */

template <class T, typename IDX>
class sparse_tpl
{
	private:
		IDX size_x;
		IDX size_y;

		T* data;
		IDX* col_ind;
		IDX* row_ptr;

		IDX data_size;
		IDX data_count;

		IDX row_size;

	public:
		sparse_tpl (){
			size_x=0;
			size_y=0;
			data_size=0;
			data_count=0;
			data = NULL;
			col_ind = NULL;
			row_ptr = NULL;
			row_size=0;
		}

		sparse_tpl( koord _size ) {
			size_x=_size.x;
			size_y=_size.y;
			data_size = 0;
			data_count = 0;
			data = NULL;
			col_ind = NULL;
			row_ptr = new IDX[ size_y + 1];
			for( IDX i = 0; i < size_y + 1; i++ ) {
				row_ptr[i] = 0;
			}
			row_size = size_y + 1;
		}

		~sparse_tpl() {
			if(data){
				delete[] data;
			}
			if(col_ind){
				delete[] col_ind;
			}
			if(row_ptr){
				delete[] row_ptr;
			}
		}

		IDX add_col(){ size_x++; return size_x; }

		IDX add_row(IDX prefetch=0){
			size_y+=1;
			if(row_size < size_y+1){
				if(row_size==0){
					row_size = size_y + 1 + prefetch;
					row_ptr = new IDX [row_size];
					for(IDX i = 0; i < size_y+1; i++){
						row_ptr[i] = 0;
					}
				}else{
					row_size = size_y + 1 + prefetch;
					IDX* new_rows = new IDX [row_size];
					for(IDX i = 0; i< size_y; i++){
						new_rows[i]=row_ptr[i];
					}
					new_rows[size_y]=new_rows[size_y-1];
					delete[] row_ptr;
					row_ptr=new_rows;
				}
			}else{
				row_ptr[size_y]=row_ptr[size_y-1];
			}
			return size_y;
		}

		void trim_row(){
			if(size_y>0 && row_ptr[size_y]==row_ptr[size_y-1]){
				size_y-=1;
			}
		}

		void clear() {
			data_count = 0;
			for( IDX i = 0; i < size_y + 1; i++ ) {
				row_ptr[i] = 0;
			}
			resize_data(0);
		}

		void reset(){
			data_count = 0;
			delete [] row_ptr;
			row_ptr=NULL;
			size_x=0;
			size_y=0;
		}

		koord get_size() const {
			return koord(size_x,size_y);
		}

		IDX get_size_x() const {
			return size_x;
		}

		IDX get_size_y() const {
			return size_y;
		}

		IDX get_data_size() const {
			return data_size;
		}

		IDX get_data_count() const {
			return data_count;
		}

		T get( IDX pos_x, IDX pos_y ) const {
			if(  pos_x >= 0  &&  pos_y >= 0  &&  pos_x < size_x  &&  pos_y < size_y  ) {
				return get_unsafe( pos_x, pos_y );
			}
			return 0;
		}

		T get( koord pos ) const {
			return get(pos.x,pos.y);
		}

		/*
		 * Access to the nonzero elements. Result is saved in pos and value!
		 */
		void get_nonzero( IDX index, koord& pos, T& value ) const {
			if(  index > data_count  ) {
				pos = koord::invalid;
				value = 0;
				return;
			}
			value = data[index];
			pos = koord(col_ind[index], 0);
			while( row_ptr[ pos.y+1 ] <= index ) {
				pos.y++;
			}
		}

		void set( IDX pos_x, IDX pos_y, T value ) {
			if(  pos_x >= 0  &&  pos_y >= 0  &&  pos_x < size_x  &&  pos_y < size_y  ) {
				set_unsafe( pos_x, pos_y, value );
			}
		}

		void set( koord pos, T value ) {
			set(pos.x, pos.y, value);
		}


	private:
		T get_unsafe( IDX pos_x, IDX pos_y ) const {
			IDX index = pos_to_index( pos_x, pos_y );
			if(  index < row_ptr[pos_y+1]  &&  col_ind[index] == pos_x  ) {
				return data[index];
			}
			else {
				return 0;
			}
		}

		void set_unsafe( IDX pos_x, IDX pos_y , T value ) {
			IDX index = pos_to_index( pos_x, pos_y );
			if(  index < row_ptr[pos_y+1]  &&  col_ind[index] == pos_x  ) {
				if( value == 0 ) {
					move_data(index+1, data_count, -1);
					for( IDX i = pos_y+1; i < size_y+1; i++ ){
						row_ptr[i]--;
					}
					data_count--;
				}
				else {
					data[index] = value;
				}
			}
			else {
				if( value == 0 ) {
					// Don't add 0 to data!
					return;
				}
				if(  data_count == data_size  ) {
					resize_data(data_size==0 ? 1 : 2*data_size);
				}
				move_data(index, data_count, 1);
				data[index] = value;
				col_ind[index] = pos_x;
				for( IDX i = pos_y+1; i < size_y+1; i++ ){
					row_ptr[i]++;
				}
				data_count++;
			}
		}

		/*
		 * Moves the elements data[start_index]..data[end_index-1] to
		 * data[start_index+offset]..data[end_index-1+offset]
		 */
		void move_data(IDX start_index, IDX end_index, sint8 offset)
		{
			IDX num = end_index - start_index;
			if(  offset < 0 ) {
				for( IDX i=0; i<num ; i++ ){
					data[start_index+i+offset] = data[start_index+i];
					col_ind[start_index+i+offset] = col_ind[start_index+i];
				}
			}
			else if(  offset > 0  ) {
				for( IDX i=num ; i>0 ; i-- ){
					data[start_index+i+offset-1] = data[start_index+i-1];
					col_ind[start_index+i+offset-1] = col_ind[start_index+i-1];
				}
			}
		}

		void resize_data( IDX new_size ) {
			if(  new_size < data_count  ||  new_size == data_size  ) {
				// new_size is too small or equal our current size.
				return;
			}

			T* new_data;
			IDX* new_col_ind;
			if(  new_size > 0  ) {
				new_data = new T[new_size];
				new_col_ind = new IDX[new_size];
			}
			else
			{
				new_data = NULL;
				new_col_ind = NULL;
			}
			if(  data_size > 0  ) {
				for( IDX i = 0; i < data_count; i++ ) {
					new_data[i] = data[i];
					new_col_ind[i] = col_ind[i];
				}
				delete[] data;
				delete[] col_ind;
			}
			data_size = new_size;
			data = new_data;
			col_ind = new_col_ind;
		}

		/*
		 * Returns an index i
		 * - to the pos (if pos already in array)
		 * - to the index, where pos can be inserted.
		 */
		IDX pos_to_index( IDX pos_x, IDX pos_y ) const {
			IDX row_start = row_ptr[ pos_y ];
			IDX row_end = row_ptr[ pos_y + 1 ];
			if (row_start >= row_end || col_ind[row_end-1] < pos_x) return row_end;
			if (col_ind[row_start]>=pos_x) return row_start;

			do {
				IDX i = (row_start + row_end) / 2;
				if( col_ind[i] >= pos_x ) {
					row_end = i;
				}
				else {
					row_start = i;
				}
			} while (row_end > row_start + 1);

			return row_end;
		}

		friend void ::swap<>(sparse_tpl<T,IDX>& a, sparse_tpl<T,IDX>& b);

		sparse_tpl(const sparse_tpl& other);
		sparse_tpl& operator=(sparse_tpl const& other);
};

template<class T, typename IDX> void swap(sparse_tpl<T,IDX>& a, sparse_tpl<T,IDX>& b)
{
	sim::swap(a.size_x, b.size_x);
	sim::swap(a.size_y, b.size_y);

	sim::swap(a.data, b.data);
	sim::swap(a.col_ind, b.col_ind);
	sim::swap(a.row_ptr, b.row_ptr);

	sim::swap(a.data_size, b.data_size);
	sim::swap(a.data_count, b.data_count);
}

#endif
