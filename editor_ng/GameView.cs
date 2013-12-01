using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;

namespace editor_ng
{
    public partial class GameView : DockContent
    {
        public GameView()
        {
            InitializeComponent();
        }


        public class UserDrawnPanel : Panel
        {
            public UserDrawnPanel()
            {
                SetStyle(ControlStyles.UserPaint, true);
            }

            protected override void OnPaintBackground(PaintEventArgs e)
            {
            }
        }
    }
}
