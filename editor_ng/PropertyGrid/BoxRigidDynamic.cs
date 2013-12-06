using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Data;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace editor_ng.PropertyGrid
{
    public partial class BoxRigidDynamic : UserControl
    {
        native.EditorServer m_editor_server;
        public BoxRigidDynamic(native.EditorServer server)
        {
            m_editor_server = server;
            InitializeComponent();
            m_editor_server.onComponentProperties += m_editor_server_onComponentProperties;
            this.VisibleChanged += (object sender, EventArgs e) =>
            {
                m_editor_server.onComponentProperties -= m_editor_server_onComponentProperties;
                if (Visible)
                {
                    m_editor_server.onComponentProperties += m_editor_server_onComponentProperties;
                }
            };
        }

        void m_editor_server_onComponentProperties(object sender, EventArgs e)
        {
            BeginInvoke(new Action(() =>
            {

                native.EditorServer.ComponentPropertiesArgs args = e as native.EditorServer.ComponentPropertiesArgs;
                for (int i = 0; i < args.names.Length; ++i)
                {
                    if (args.names[i] == "dynamic")
                    {
                        dynamicCheckbox.Checked = args.values[i] == "true";
                    }
                    else if (args.names[i] == "size")
                    {
                        string s = args.values[i].Replace('.', ',');
                        var strs = s.Split(new char[] { ';' });
                        xNumericUpDown.Value = (decimal)float.Parse(strs[0]);
                        yNumericUpDown.Value = (decimal)float.Parse(strs[1]);
                        zNumericUpDown.Value = (decimal)float.Parse(strs[2]);
                    }
                }
            }));
        }

        private void dynamicCheckbox_CheckedChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("box_rigid_actor", "dynamic", dynamicCheckbox.Checked ? "true" : "false");
        }

        private void sendSize()
        {
            m_editor_server.setComponentProperty("box_rigid_actor", "size", xNumericUpDown.Value.ToString() + ";" + yNumericUpDown.Value.ToString() + ";" + zNumericUpDown.Value.ToString()); 
        }

        private void xNumericUpDown_ValueChanged(object sender, EventArgs e)
        {
            sendSize();
        }

        private void yNumericUpDown_ValueChanged(object sender, EventArgs e)
        {
            sendSize();
        }

        private void zNumericUpDown_ValueChanged(object sender, EventArgs e)
        {
            sendSize();
        }
    }
}
