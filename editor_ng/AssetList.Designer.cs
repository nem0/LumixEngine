namespace editor_ng
{
    partial class AssetList
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.mainTreeView = new System.Windows.Forms.TreeView();
            this.importModelButton = new System.Windows.Forms.Button();
            this.SuspendLayout();
            // 
            // mainTreeView
            // 
            this.mainTreeView.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.mainTreeView.Location = new System.Drawing.Point(0, 29);
            this.mainTreeView.Name = "mainTreeView";
            this.mainTreeView.Size = new System.Drawing.Size(284, 233);
            this.mainTreeView.TabIndex = 0;
            this.mainTreeView.ItemDrag += new System.Windows.Forms.ItemDragEventHandler(this.mainTreeView_ItemDrag);
            // 
            // importModelButton
            // 
            this.importModelButton.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.importModelButton.Location = new System.Drawing.Point(0, 0);
            this.importModelButton.Name = "importModelButton";
            this.importModelButton.Size = new System.Drawing.Size(284, 23);
            this.importModelButton.TabIndex = 1;
            this.importModelButton.Text = "Import collada (*.DAE)";
            this.importModelButton.UseVisualStyleBackColor = true;
            this.importModelButton.Click += new System.EventHandler(this.importModelButton_Click);
            // 
            // AssetList
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(284, 262);
            this.Controls.Add(this.importModelButton);
            this.Controls.Add(this.mainTreeView);
            this.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(238)));
            this.HideOnClose = true;
            this.Name = "AssetList";
            this.Text = "Assets";
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.TreeView mainTreeView;
        private System.Windows.Forms.Button importModelButton;
    }
}