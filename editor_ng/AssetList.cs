using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;

namespace editor_ng
{
    public partial class AssetList : DockContent
    {
        public MainForm main_form { get; set; }

        private native.EditorServer m_server;
        private MainForm m_main_form;

        public native.EditorServer server
        {
            get { return m_server; }
            set
            {
                if (m_server != null)
                {
                    m_server.onEntitySelected -= onEntitySelected;
                }
                m_server = value;
                if (m_server != null)
                {
                    m_server.onEntitySelected += onEntitySelected;
                }
            }
        }


        public AssetList(MainForm main_form)
        {
            m_main_form = main_form;
            InitializeComponent();
            
            string root = System.IO.Directory.GetCurrentDirectory();

            TreeNode root_node = new TreeNode(root);
            root_node.Tag = root;
            mainTreeView.Nodes.Add(root_node);
            expand(root, root_node); 

            mainTreeView.NodeMouseDoubleClick += onDoubleClick;
        }

        public void refresh()
        {
            string root = System.IO.Directory.GetCurrentDirectory();
            this.BeginInvoke(new Action(() =>
            {
                mainTreeView.Nodes.Clear();
                TreeNode root_node = new TreeNode(root);
                root_node.Tag = root;
                mainTreeView.Nodes.Add(root_node);
                expand(root, root_node);
            }));
        }

        private int m_selected_entity;

        private void onEntitySelected(object sender, EventArgs args)
        {
            native.EditorServer.EntitySelectedArgs e = args as native.EditorServer.EntitySelectedArgs;
            m_selected_entity = e.uid;
        }

        private bool isAsset(string filename)
        {
            return filename.EndsWith(".scene.xml")
                || filename.EndsWith(".unv");
        }

        private void expand(string root, TreeNode root_node)
        {
            if (root_node.Nodes.Count == 0)
            {
                string[] dirs = System.IO.Directory.GetDirectories(root);
                foreach (string dir in dirs)
                {
                    TreeNode node = new TreeNode(System.IO.Path.GetFileName(dir));
                    node.Tag = dir;
                    root_node.Nodes.Add(node);
                }


                string[] files = System.IO.Directory.GetFiles(root);
                foreach (string file in files)
                {
                    if (isAsset(file))
                    {
                        TreeNode node = new TreeNode(System.IO.Path.GetFileName(file));
                        node.Tag = file;
                        root_node.Nodes.Add(node);
                    }
                }
            }
            root_node.Expand();
        }

        private void onDoubleClick(object sender, TreeNodeMouseClickEventArgs args)
        {
            string root = args.Node.Tag as string;

            if (System.IO.File.Exists(root))
            {
                string dir = System.IO.Directory.GetCurrentDirectory();
                string file = root;
                file = file.Replace(dir, "");
                file = file.TrimStart('/', '\\');
                if (file.IndexOf("scene.xml") >= 0)
                {
                    server.createComponent(Crc32.Compute("renderable"));
                    server.setComponentProperty("renderable", "source", file);
                    server.requestComponentProperties("renderable");
                }
                else if (file.IndexOf(".unv") >= 0)
                {
                    server.openUniverse(file);
                }
            }
            else
            {
                expand(root, args.Node);
            }
        }

        private void mainTreeView_ItemDrag(object sender, ItemDragEventArgs e)
        {
            DoDragDrop((e.Item as TreeNode).Tag as string, DragDropEffects.Link);
        }

        private void importModelButton_Click(object sender, EventArgs e)
        {
            m_main_form.importModel();
        }
    }
}
