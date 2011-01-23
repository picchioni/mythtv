#ifndef VIDEOSETUPWIZARD_H
#define VIDEOSETUPWIZARD_H

#include <uitypes.h>
#include <mythwidgets.h>
#include <mythdialogs.h>

// Utility headers
#include <videodisplayprofile.h>

// libmythui
#include <mythuibutton.h>
#include <mythuibuttonlist.h>
#include <mythscreentype.h>
#include <mythdialogbox.h>

extern const QString VIDEO_SAMPLE_HD_LOCATION;
extern const QString VIDEO_SAMPLE_SD_LOCATION;

class VideoSetupWizard : public MythScreenType
{
  Q_OBJECT

  public:

    VideoSetupWizard(MythScreenStack *parent, MythScreenType *general,
                     MythScreenType *audio, const char *name = 0);
    ~VideoSetupWizard();

    bool Create(void);
    bool keyPressEvent(QKeyEvent *);
    void customEvent(QEvent *e);

    void save(void);

  private:
    enum TestType
    {
        ttNone = 0,
        ttHighDefinition,
        ttStandardDefinition
    };

    QString              m_downloadFile;
    TestType             m_testType;

    MythScreenType      *m_generalScreen;
    MythScreenType      *m_audioScreen;

    MythUIButtonList    *m_playbackProfileButtonList;

    MythUIButton        *m_testSDButton;
    MythUIButton        *m_testHDButton;

    MythUIButton        *m_nextButton;
    MythUIButton        *m_prevButton;

    VideoDisplayProfile *m_vdp;

  private slots:
    void slotNext(void);
    void slotPrevious(void);
    void loadData(void);

    void testSDVideo(void);
    void testHDVideo(void);
    void playVideoTest(QString desc,
                       QString title,
                       QString file);

    void DownloadSample(QString url);
};

#endif
