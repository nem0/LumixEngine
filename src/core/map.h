#pragma once


#include <new>
#include "core/default_allocator.h"
#include "core/lux.h"
#include "core/math_utils.h"


namespace Lux
{


template <typename Key, typename Value, typename Allocator = DefaultAllocator>
class Map
{
	private:
		struct Node
		{
			Node() { left = right = NULL; height = 1; }
			Key		key;
			Value	value;
			Node*	left;
			Node*	right;
			Node*	parent;
			int		height;
			int getLeftHeight() { return left ? left->height : 0; }
			int getRightHeight() { return right ? right->height : 0; }
		};

	public:
		struct iterator
		{
			iterator(Node* node)
			{
				this->node = node;
			}

			Value& second()
			{
				return node->value;
			}

			Key& first()
			{
				return node->key;
			}

			bool operator !=(const iterator& rhs)
			{
				return node != rhs.node;
			}

			bool operator ==(const iterator& rhs)
			{
				return node == rhs.node;
			}

			void operator ++()
			{
				if(node->left != 0)
					node = node->left;
				else if(node->right != 0)
					node = node->right;
				else if(node->parent != 0)
				{
					if(node->parent->left == node && node->parent->right != 0)
					{
						node = node->parent->right;
					}
					else
					{
						Node* tmp = node;
						do
						{
							tmp = node;
							node = node->parent;
						}
						while(node && (node->right == 0 || node->right == tmp));
						if(node)
						{
							node = node->right;
						}
					}
				}
				else
				{
					node = 0;
				}
			}

			private:
				Node* node;
		};

	public:
		Map(const Allocator& allocator)
			: m_allocator(allocator)
		{
			m_root = NULL;
			m_size = 0;
		}

		Map()
		{
			m_root = NULL;
			m_size = 0;
		}

		~Map()
		{
			clear();
		}

		int size()
		{
			return m_size;
		}

		void clear()
		{
			clearNode(m_root);
			m_root = 0;
		}

		iterator begin() const
		{
			return iterator(m_root);
		}

		iterator end() const 
		{
			return iterator(0);
		}

		bool contains(const Key& key)
		{
			return _find(key) != NULL;
		}

		iterator find(const Key& key) const
		{
			Node* node = _find(key);
			if(node != NULL && node->key == key)
			{
				return iterator(node);
			}
			return iterator(NULL);
		}


		bool find(const Key& key, Value& value) const
		{
			Node* node = _find(key);
			if(node != 0 && node->key == key)
			{
				value = node->value;
				return true;
			}
			return false;
		}

		Value& operator[](const Key& key)
		{
			Node* node = _find(key);
			if(!node || node->key != key)
			{
				Node* new_node = new ((Node*)m_allocator.allocate(sizeof(Node))) Node();
				++m_size;
				new_node->key = key;
				insert(key, m_root, NULL, new_node);
				return new_node->value;
			}

			return node->value;
		}

		void insert(const Key& key, const Value& value)
		{
			Node* new_node = new ((Node*)m_allocator.allocate(sizeof(Node))) Node();
			++m_size;
			new_node->key = key;
			new_node->value = value;
			insert(key, m_root, 0, new_node);
		}

		void erase(const Key& key)
		{
			m_root = deleteNode(key, m_root);
		}

	private:
		void clearNode(Node* node)
		{
			if(node)
			{
				clearNode(node->left);
				clearNode(node->right);
				--m_size;
				node->value.~Value();
				m_allocator.deallocate(node);
			}
		}

		Node* rotateLeft(Node*& node)
		{
			Node* rightChild = node->right;
			node->right = rightChild->left;
			if(node->right)
				node->right->parent = node;
			rightChild->left = node;
			rightChild->parent = node->parent;
			if(rightChild->left)
				rightChild->left->parent = rightChild;
			rightChild->height = Math::maxValue(rightChild->getLeftHeight(), rightChild->getRightHeight()) + 1;
			node->height = Math::maxValue(node->getLeftHeight(), node->getRightHeight()) + 1;
			node = rightChild;
			return node;
		}

		Node* rotateRight(Node*& node)
		{
			Node* leftChild = node->left;
			node->left = leftChild->right;
			if(node->left)
				node->left->parent = node;
			leftChild->right = node;
			leftChild->parent = node->parent;
			if(leftChild->right)
				leftChild->right->parent = leftChild;

			leftChild->height = Math::maxValue(leftChild->getLeftHeight(), leftChild->getRightHeight()) + 1;
			node->height = Math::maxValue(node->getLeftHeight(), node->getRightHeight()) + 1;
			
			node = leftChild;
			return node;
		}


		Node* insert(const Key& key, Node*& node, Node* parent, Node* new_node)
		{
			Node* ret = node;
			if(node == 0)
			{
				node = new_node;
				node->parent = parent;
				ret = node;
			}
			else if(key < node->key)
			{
				node->left = insert(key, node->left, node, new_node);
				if(node->getLeftHeight() - node->getRightHeight() == 2)
				{
					if(key < node->left->key)
					{
						ret = rotateRight(node);
					}
					else
					{
						node->left = rotateLeft(node->left);
						ret = rotateRight(node);
					}
				}
			}
			else if(node->key < key)
			{
				node->right = insert(key, node->right, node, new_node);
				if(node->getRightHeight() - node->getLeftHeight() == 2)
				{
					if(node->right->key < key)
					{
						ret = rotateLeft(node);
					}
					else
					{
						node->right = rotateRight(node->right);
						ret = rotateLeft(node);
					}
				}
			}
			else
			{
				ASSERT(false); // key == node->key -> key already in tree
			}
			ret->height = Math::maxValue(node->getLeftHeight(), node->getRightHeight()) + 1;
			return ret;
		}


		Node* _find(const Key& key) const
		{
			Node* node = m_root;
			Node* found = 0;
			while(node)
			{
				if(key < node->key)
				{
					node = node->left;
				}
				else
				{
					found = node;
					node = node->right;
				}
			}
			return found;
		}

		Node* deleteNode(const Key& key, Node* root)
		{
			if (root == NULL)
				return root;
			if ( key < root->key )
				root->left = deleteNode(key, root->left);
			else if( key > root->key )
				root->right = deleteNode(key, root->right);
			else
			{
				if( (root->left == NULL) || (root->right == NULL) )
				{
					Node *temp = root->left ? root->left : root->right;

					if(temp == NULL)
					{
						temp = root;
						root = NULL;
					}
					else 
					{
						temp->parent = root->parent;
						if(root->parent)
						{
							if(root->parent->left == root)
							{
								root->parent->left = temp;
							}
							else
							{
								root->parent->right = temp;
							}
						}
						Node* swap = temp;
						temp = root;
						root = swap;
					}

					--m_size; 
					temp->value.~Value();
					m_allocator.deallocate(temp);
				}
				else
				{
					Node* temp = getMinValueNode(root->right);
					swap(temp, root);
					root->right = deleteNode(temp->key, root->right);
				}
			}

			if (root == NULL)
				return root;

			root->height = Math::maxValue(root->getLeftHeight(), root->getRightHeight()) + 1;

			int balance = root->getLeftHeight() - root->getRightHeight();

			int left_balance = root->left ? (root->left->getLeftHeight() - root->left->getRightHeight()) : 0;
			if (balance > 1 && left_balance >= 0)
				return rotateRight(root);

			if (balance > 1 && left_balance < 0)
			{
				root->left = rotateLeft(root->left);
				return rotateRight(root);
			}

			int right_balance = root->right ? (root->right->getLeftHeight() - root->right->getRightHeight()) : 0;
			if (balance < -1 && right_balance <= 0)
				return rotateLeft(root);

			// Right Left Case
			if (balance < -1 && right_balance > 0)
			{
				root->right = rotateRight(root->right);
				return rotateLeft(root);
			}

			return root;
		}	

		void swap(Node*& a, Node*& b)
		{
			Node* tmp = a->left;
			a->left = b->left;
			b->left = tmp;
			if(a->left)
			{
				a->left->parent = a;
			}
			if(b->left)
			{
				b->left->parent = b;
			}

			tmp = a->right;
			a->right = b->right;
			b->right = tmp;
			if(a->right)
			{
				a->right->parent = a;
			}
			if(b->right)
			{
				b->right->parent = b;
			}

			tmp = a->parent;
			a->parent = b->parent;
			if(b->parent)
			{
				if(b->parent->left == b)
				{
					a->parent->left = a;
				}
				else
				{
					a->parent->right = a;
				}
			}
			b->parent = tmp;
			if(tmp)
			{
				if(tmp->left == a)
				{
					tmp->left = b;
				}
				else
				{
					tmp->right = b;
				}
			}
			tmp = a;
			a = b;
			b = tmp;
		}

		Node* getMinValueNode(Node* node)
		{
			Node* current = node;
 
			while (current->left != NULL)
				current = current->left;
 
			return current;
		}

	private:
		Node*	m_root;
		int		m_size;
		Allocator m_allocator;
};


} // !namespace Lux
