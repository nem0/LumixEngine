#pragma once


#include <qdialog.h>


namespace Ui {
	class CreateTextureDialog;
} // namespace Ui


class CreateTextureDialog : public QDialog
{
	public:
		explicit CreateTextureDialog(QWidget* paren, const QString& dir);
		~CreateTextureDialog();

	private:
		void onAccepted();

	private:
		Ui::CreateTextureDialog* m_ui;
		QString m_dir;
};