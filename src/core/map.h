#pragma once


#include "core/lux.h"
#include <cassert>


#ifndef max
#define max(x, y) ((x) > (y) ? (x) : (y))
#endif


namespace Lux
{


template <class Key, class Value>
class map
{
	private:
		struct Node
		{
			Node() { left = right = 0; height = 1; }
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
		map()
		{
			mRoot = 0;
			mSize = 0;
		}

		int size()
		{
			return mSize;
		}

		void clear()
		{
			clearNode(mRoot);
			mRoot = 0;
		}

		iterator begin() const
		{
			return iterator(mRoot);
		}

		iterator end() const 
		{
			return iterator(0);
		}

		iterator find(const Key& key) const
		{
			Node* node = _find(key);
			if(node != 0 && node->key == key)
			{
				return iterator(node);
			}
			return iterator(0);
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
				insert(key, Value());
				node = _find(key);
			}

			return node->value;
		}

		void insert(const Key& key, const Value& value)
		{
			insert(key, value, mRoot, 0);
			++mSize;
		}

		void erase(const Key& key)
		{
			mRoot = deleteNode(key, mRoot);
		}

	private:
		void clearNode(Node* node)
		{
			if(node)
			{
				clearNode(node->left);
				clearNode(node->right);
				delete node;
			}
		}

		Node* rotateRight(Node*& node)
		{
			Node* rightChild = node->right;
			node->right = rightChild->left;
			if(node->right)
				node->right->parent = node;
			rightChild->left = node;
			rightChild->parent = node->parent;
			if(rightChild->left)
				rightChild->left->parent = rightChild;
			rightChild->height = max(rightChild->getLeftHeight(), rightChild->getRightHeight()) + 1;
			node->height = max(node->getLeftHeight(), node->getRightHeight()) + 1;
			node = rightChild;
			return node;
		}

		void doubleRotateRight(Node*& node)
		{
			rotateLeft(node->right);
			rotateRight(node);
		}

		Node* rotateLeft(Node*& node)
		{
			Node* leftChild = node->left;
			node->left = leftChild->right;
			if(node->left)
				node->left->parent = node;
			leftChild->right = node;
			leftChild->parent = node->parent;
			if(leftChild->right)
				leftChild->right->parent = leftChild;
			leftChild->height = max(leftChild->getLeftHeight(), leftChild->getRightHeight()) + 1;
			node->height = max(node->getLeftHeight(), node->getRightHeight()) + 1;
			node = leftChild;
			return node;
		}

		void doubleRotateLeft(Node*& node)
		{
			rotateRight(node->left);
			rotateLeft(node);
		}

		void insert(const Key& key, const Value& value, Node*& node, Node* parent)
		{
			if(node == 0)
			{
				node = new Node();
				node->key = key;
				node->value = value;
				node->parent = parent;
			}
			else if(key < node->key)
			{
				insert(key, value, node->left, node);
				if(node->getLeftHeight() - node->getRightHeight() == 2)
				{
					if(key < node->left->key)
					{
						rotateLeft(node);
					}
					else
					{
						doubleRotateLeft(node);
					}
				}
			}
			else if(key > node->key)
			{
				insert(key, value, node->right, node);
				if(node->getRightHeight() - node->getLeftHeight() == 2)
				{
					if(key > node->right->key)
					{
						rotateRight(node);
					}
					else
					{
						doubleRotateRight(node);
					}
				}
			}
			else
			{
				assert(false); // key == node->key -> key already in tree
			}
			node->height = max(node->getLeftHeight(), node->getRightHeight()) + 1;
		}


		Node* _find(const Key& key) const
		{
			Node* node = mRoot;
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
						*root = *temp;

					--mSize; 
					delete temp;
				}
				else
				{
					Node* temp = getMinValueNode(root->right);
					root->key = temp->key;
					root->right = deleteNode(temp->key, root->right);
				}
			}

			if (root == NULL)
				return root;

			root->height = max(root->getLeftHeight(), root->getRightHeight()) + 1;

			int balance = root->getLeftHeight() - root->getRightHeight();

			int left_balance = (root->left->getLeftHeight() - root->left->getRightHeight());
			if (balance > 1 && left_balance >= 0)
				return rotateRight(root);

			if (balance > 1 && left_balance < 0)
			{
				root->left =  rotateLeft(root->left);
				return rotateRight(root);
			}

			int right_balance = (root->right->getLeftHeight() - root->right->getRightHeight());
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

		Node* getMinValueNode(Node* node)
		{
			Node* current = node;
 
			while (current->left != NULL)
				current = current->left;
 
			return current;
		}

	private:
		Node*	mRoot;
		int		mSize;
};


} // !namespace Lux