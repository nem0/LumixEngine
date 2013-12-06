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
                    if (args.names[i] == Crc32.Compute("dynamic"))
                    {
                        dynamicCheckbox.Checked = BitConverter.ToBoolean(args.values[i], 0);
                    }
                    else if (args.names[i] == Crc32.Compute("size"))
                    {
                        xNumericUpDown.Value = (decimal)BitConverter.ToSingle(args.values[i], 0);
                        yNumericUpDown.Value = (decimal)BitConverter.ToSingle(args.values[i], 4);
                        zNumericUpDown.Value = (decimal)BitConverter.ToSingle(args.values[i], 8);
                    }
                }
            }));
        }

        private void dynamicCheckbox_CheckedChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("box_rigid_actor", "dynamic", dynamicCheckbox.Checked);
        }

        private void sendSize()
        {
            m_editor_server.setComponentProperty("box_rigid_actor", "size", xNumericUpDown.Value); 
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
